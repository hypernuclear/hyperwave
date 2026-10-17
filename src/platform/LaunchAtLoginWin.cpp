#include "LaunchAtLogin.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#include <string>

namespace hyperwave::launchAtLogin {

namespace {

// A shortcut in the Startup folder, not an HKCU\...\Run value.
//
// The shortcut is visible to the user in Explorer and in Task Manager's
// Startup tab, which is where people go to turn things off. A Run value
// is invisible in Explorer and reads as something an app did behind
// their back.
constexpr wchar_t kShortcutName[] = L"\\hyperwave.lnk";

QString shortcutPath()
{
    PWSTR startup = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &startup);
    if (FAILED(hr) || !startup) {
        if (startup)
            CoTaskMemFree(startup);
        qWarning("hyperwave: cannot locate the Startup folder (hr 0x%08lx)",
                 static_cast<unsigned long>(hr));
        return {};
    }
    QString path = QString::fromWCharArray(startup) + QString::fromWCharArray(kShortcutName);
    CoTaskMemFree(startup);
    return path;
}

bool createShortcut(const QString &path)
{
    IShellLinkW *link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, reinterpret_cast<void **>(&link));
    if (FAILED(hr)) {
        qWarning("hyperwave: CoCreateInstance(ShellLink) failed (hr 0x%08lx)",
                 static_cast<unsigned long>(hr));
        return false;
    }

    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString dir = QDir::toNativeSeparators(QFileInfo(exe).absolutePath());
    const std::wstring wExe = exe.toStdWString();
    const std::wstring wDir = dir.toStdWString();

    link->SetPath(wExe.c_str());
    link->SetWorkingDirectory(wDir.c_str());
    link->SetDescription(L"System audio visualiser for the taskbar");

    IPersistFile *file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file));
    if (SUCCEEDED(hr)) {
        const std::wstring wPath = path.toStdWString();
        hr = file->Save(wPath.c_str(), TRUE);
        file->Release();
    }
    link->Release();

    if (FAILED(hr)) {
        qWarning("hyperwave: could not write the startup shortcut (hr 0x%08lx)",
                 static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

} // namespace

bool supported()
{
    return true;
}

bool isEnabled()
{
    const QString path = shortcutPath();
    return !path.isEmpty() && QFileInfo::exists(path);
}

bool setEnabled(bool enabled)
{
    const QString path = shortcutPath();
    if (path.isEmpty())
        return false;

    if (enabled) {
        createShortcut(path);
    } else {
        const std::wstring wPath = path.toStdWString();
        if (!DeleteFileW(wPath.c_str())) {
            const DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND)
                qWarning("hyperwave: could not remove the startup shortcut (error %lu)", err);
        }
    }
    // Same reasoning as the macOS side: report what is actually on disk.
    return isEnabled();
}

} // namespace hyperwave::launchAtLogin
