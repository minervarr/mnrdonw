#include "desktop_entry.hh"

std::string desktopEntryText(std::string_view execPath) {
    if (execPath.empty()) return {};
    if (execPath.find('"') != std::string_view::npos) return {};
    if (execPath.find('\n') != std::string_view::npos) return {};
    if (execPath.find('\r') != std::string_view::npos) return {};
    std::string text;
    text += "[Desktop Entry]\n";
    text += "Type=Application\n";
    text += "Version=1.0\n";
    text += "Name=mnrdonw\n";
    text += "Terminal=false\n";
    text += "Categories=Office;TextEditor;\n";
    text += "MimeType=text/markdown;text/x-markdown;\n";
    text += "StartupWMClass=io.nava.mnrdonw\n";
    text += "Exec=\"";
    text += execPath;
    text += "\" %f\n";
    return text;
}
