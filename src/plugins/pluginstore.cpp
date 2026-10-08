#include "plugins/pluginstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>

#include <optional>

#include "moc_pluginstore.cpp"
#include "plugins/pluginmanifest.h"
#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginStore");

/// Archive formats the store knows how to list and extract.
enum class ArchiveFormat {
    Zip,
    TarGz,
};

/// Time budget for reading an archive's table of contents.
constexpr int kArchiveListTimeoutMs = 30000;
/// Time budget for extracting an archive.
constexpr int kArchiveExtractTimeoutMs = 60000;

bool copyDirectoryRecursively(const QString& source, const QString& destination,
        QString* pError) {
    QDir sourceDir(source);
    if (!sourceDir.exists()) {
        if (pError != nullptr) {
            *pError = QStringLiteral("Source directory does not exist: %1").arg(source);
        }
        return false;
    }
    if (!QDir().mkpath(destination)) {
        if (pError != nullptr) {
            *pError = QStringLiteral("Cannot create directory: %1").arg(destination);
        }
        return false;
    }

    QDirIterator it(source, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QString relative = sourceDir.relativeFilePath(sourcePath);
        const QString targetPath = QDir(destination).filePath(relative);
        const QFileInfo info(sourcePath);
        if (info.isDir()) {
            if (!QDir().mkpath(targetPath)) {
                if (pError != nullptr) {
                    *pError = QStringLiteral("Cannot create directory: %1").arg(targetPath);
                }
                return false;
            }
        } else {
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            if (QFile::exists(targetPath)) {
                QFile::remove(targetPath);
            }
            if (!QFile::copy(sourcePath, targetPath)) {
                if (pError != nullptr) {
                    *pError = QStringLiteral("Cannot copy '%1' to '%2'")
                                      .arg(sourcePath, targetPath);
                }
                return false;
            }
        }
    }
    return true;
}

/// Captures the standard output of `program` with `arguments`, or returns
/// std::nullopt if the process could not be started or exited unsuccessfully.
std::optional<QByteArray> runProcessCapture(
        const QString& program, const QStringList& arguments) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(kArchiveListTimeoutMs) ||
            process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0) {
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

/// True if `entryName` is a relative path that cannot escape the extraction
/// root: no absolute prefix, no drive letter and no ".." component.
bool isSafeArchiveEntryName(const QString& entryName) {
    QString name = entryName;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.isEmpty() || name.startsWith(QLatin1Char('/'))) {
        return false;
    }
    if (name.size() >= 2 && name.at(1) == QLatin1Char(':')) {
        return false; // Windows drive letter, e.g. "C:/..."
    }
    return !name.split(QLatin1Char('/')).contains(QLatin1String(".."));
}

/// Rejects archives whose members could write outside the extraction
/// directory (zip-slip / tar path traversal) or that contain links, which can
/// be used to escape it. Must run before extraction.
bool archiveEntriesAreSafe(
        const QString& archivePath, ArchiveFormat format, QString* pError) {
    const bool isZip = format == ArchiveFormat::Zip;
    const QString program = isZip ? QStringLiteral("unzip") : QStringLiteral("tar");

    const QStringList nameArguments = isZip
            ? QStringList{QStringLiteral("-Z1"), archivePath}
            : QStringList{QStringLiteral("-tf"), archivePath};
    const std::optional<QByteArray> names =
            runProcessCapture(program, nameArguments);
    if (!names) {
        if (pError != nullptr) {
            *pError = PluginStore::tr("Failed to read the archive contents.");
        }
        return false;
    }
    const QStringList entries = QString::fromLocal8Bit(*names)
                                        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        const QString name = entry.trimmed();
        if (!name.isEmpty() && !isSafeArchiveEntryName(name)) {
            if (pError != nullptr) {
                *pError = PluginStore::tr(
                        "Archive contains a path that escapes the plugin "
                        "directory: %1")
                                  .arg(name);
            }
            return false;
        }
    }

    // The verbose listing exposes Unix file types: 'l' for symlinks and 'h'
    // for hard links. Both can be used to write outside the destination.
    const QStringList verboseArguments = isZip
            ? QStringList{QStringLiteral("-Z"), QStringLiteral("-l"), archivePath}
            : QStringList{QStringLiteral("-tvf"), archivePath};
    const std::optional<QByteArray> verbose =
            runProcessCapture(program, verboseArguments);
    if (!verbose) {
        if (pError != nullptr) {
            *pError = PluginStore::tr("Failed to read the archive contents.");
        }
        return false;
    }
    const QStringList lines = QString::fromLocal8Bit(*verbose)
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QChar type = line.at(0);
        if (type == QLatin1Char('l') || type == QLatin1Char('h')) {
            if (pError != nullptr) {
                *pError = PluginStore::tr(
                        "Archive contains a link, which is not allowed.");
            }
            return false;
        }
    }
    return true;
}

} // anonymous namespace

PluginStore::PluginStore(QNetworkAccessManager* pNetworkManager,
        UserSettingsPointer pConfig,
        QObject* pParent)
        : QObject(pParent),
          m_pNetworkManager(pNetworkManager),
          m_pConfig(std::move(pConfig)) {
}

QString PluginStore::userPluginDir() const {
    return QDir(m_pConfig->getSettingsPath()).filePath(QStringLiteral("plugins"));
}

QString PluginStore::findManifestDirectory(const QString& root) const {
    if (QFileInfo::exists(QDir(root).filePath(QStringLiteral("plugin.json")))) {
        return root;
    }
    // Allow archives that wrap the plugin in a single top-level directory.
    const QDir rootDir(root);
    const QStringList entries = rootDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& entry : entries) {
        const QString candidate = rootDir.filePath(entry);
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("plugin.json")))) {
            return candidate;
        }
    }
    return QString();
}

bool PluginStore::installExtractedDirectory(const QString& extractedDirectory,
        QString* pPluginId, QString* pError) {
    const QString manifestDir = findManifestDirectory(extractedDirectory);
    if (manifestDir.isEmpty()) {
        if (pError != nullptr) {
            *pError = tr("No plugin.json found in the archive or directory.");
        }
        return false;
    }

    const QString manifestPath = QDir(manifestDir).filePath(QStringLiteral("plugin.json"));
    QString parseError;
    const auto manifest = PluginManifestReader::read(manifestPath, &parseError);
    if (!manifest) {
        if (pError != nullptr) {
            *pError = tr("Invalid plugin manifest: %1").arg(parseError);
        }
        return false;
    }

    const QString targetDir = QDir(userPluginDir()).filePath(manifest->id);
    if (QFileInfo::exists(targetDir)) {
        if (pError != nullptr) {
            *pError = tr("A plugin with id '%1' is already installed.").arg(manifest->id);
        }
        return false;
    }

    QString copyError;
    if (!copyDirectoryRecursively(manifestDir, targetDir, &copyError)) {
        // Do not leave a half-installed plugin behind that would block a retry.
        QDir(targetDir).removeRecursively();
        if (pError != nullptr) {
            *pError = copyError;
        }
        return false;
    }

    if (pPluginId != nullptr) {
        *pPluginId = manifest->id;
    }
    kLogger.info() << "Installed plugin" << manifest->id << "to" << targetDir;
    emit installed(manifest->id);
    return true;
}

bool PluginStore::installFromDirectory(
        const QString& sourceDirectory, QString* pPluginId, QString* pError) {
    return installExtractedDirectory(sourceDirectory, pPluginId, pError);
}

bool PluginStore::installFromArchive(
        const QString& archivePath, QString* pPluginId, QString* pError) {
    QFileInfo info(archivePath);
    if (!info.exists()) {
        if (pError != nullptr) {
            *pError = tr("Archive does not exist: %1").arg(archivePath);
        }
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (pError != nullptr) {
            *pError = tr("Cannot create a temporary directory.");
        }
        return false;
    }

    const QString suffix = info.suffix().toLower();
    std::optional<ArchiveFormat> format;
    if (suffix == QLatin1String("zip")) {
        format = ArchiveFormat::Zip;
    } else if (suffix == QLatin1String("gz") ||
            suffix == QLatin1String("tgz") ||
            info.fileName().endsWith(QLatin1String(".tar.gz"))) {
        format = ArchiveFormat::TarGz;
    }
    if (!format) {
        if (pError != nullptr) {
            *pError = tr("Unsupported archive format '%1' (use .zip or .tar.gz).")
                              .arg(suffix);
        }
        return false;
    }
    if (!archiveEntriesAreSafe(info.absoluteFilePath(), *format, pError)) {
        return false;
    }

    QProcess process;
    if (*format == ArchiveFormat::Zip) {
        process.start(QStringLiteral("unzip"),
                {QStringLiteral("-q"), QStringLiteral("-o"), info.absoluteFilePath(),
                        QStringLiteral("-d"), tempDir.path()});
    } else {
        process.start(QStringLiteral("tar"),
                {QStringLiteral("-xf"), info.absoluteFilePath(), QStringLiteral("-C"),
                        tempDir.path()});
    }

    if (!process.waitForFinished(kArchiveExtractTimeoutMs) ||
            process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0) {
        if (pError != nullptr) {
            *pError = tr("Failed to extract archive: %1")
                              .arg(QString::fromLocal8Bit(process.readAllStandardError()));
        }
        return false;
    }

    return installExtractedDirectory(tempDir.path(), pPluginId, pError);
}

void PluginStore::installFromUrl(const QUrl& url,
        const QString& expectedSha256,
        InstallCallback callback) {
    if (m_pNetworkManager == nullptr) {
        callback(false, QString(), tr("Networking is unavailable."));
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* pReply = m_pNetworkManager->get(request);
    connect(pReply, &QNetworkReply::finished, this, [this, pReply, expectedSha256, callback]() {
        pReply->deleteLater();
        if (pReply->error() != QNetworkReply::NoError) {
            callback(false, QString(), tr("Download failed: %1").arg(pReply->errorString()));
            return;
        }

        const QByteArray data = pReply->readAll();
        if (!expectedSha256.isEmpty()) {
            const QString actual = QString::fromLatin1(
                    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (actual.compare(expectedSha256, Qt::CaseInsensitive) != 0) {
                callback(false,
                        QString(),
                        tr("Checksum mismatch.\nExpected: %1\nActual:   %2")
                                .arg(expectedSha256, actual));
                return;
            }
        }

        // Determine a file name for the download.
        QString fileName = QFileInfo(pReply->url().path()).fileName();
        if (fileName.isEmpty()) {
            fileName = QStringLiteral("plugin.zip");
        }
        QTemporaryFile tempFile(QDir::tempPath() +
                QStringLiteral("/mixxx-plugin-XXXXXX.") +
                QFileInfo(fileName).suffix());
        if (!tempFile.open()) {
            callback(false, QString(), tr("Cannot create a temporary file."));
            return;
        }
        tempFile.write(data);
        tempFile.flush();
        tempFile.close();

        QString pluginId;
        QString error;
        if (!installFromArchive(tempFile.fileName(), &pluginId, &error)) {
            callback(false, QString(), error);
            return;
        }
        callback(true, pluginId, QString());
    });
}

} // namespace plugins
} // namespace mixxx
