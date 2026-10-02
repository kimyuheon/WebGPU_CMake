#include "lot_ui_command.h"

namespace lot_ui {

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) out += ' ';
            else out += c;
        }
    }
    out += '"';
    return out;
}

std::string commandJson(const Command& c) {
    std::string j = "{\"id\":" + quote(c.id)
                  + ",\"label\":" + quote(c.label)
                  + ",\"key\":" + quote(c.keyCode)
                  + ",\"ctrl\":" + (c.ctrl ? "1" : "0")
                  + ",\"shortcut\":" + quote(c.shortcut)
                  + ",\"tip\":" + quote(c.tip)
                  + ",\"state\":" + quote(c.state);
    if (!c.icon.empty()) j += ",\"icon\":" + quote(c.icon);
    if (c.separatorBefore) j += ",\"sep\":1";
    if (!c.menu.empty()) {
        j += ",\"menuTitle\":" + quote(c.menuTitle) + ",\"menu\":[";
        for (size_t i = 0; i < c.menu.size(); ++i) {
            if (i) j += ",";
            j += commandJson(c.menu[i]);
        }
        j += "]";
    }
    j += "}";
    return j;
}

}  // namespace lot_ui
