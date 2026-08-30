import os

def bundle():
    with open('app/index.html', 'r', encoding='utf-8') as f:
        html = f.read()
    with open('app/css/style.css', 'r', encoding='utf-8') as f:
        css = f.read()
    with open('app/js/app.js', 'r', encoding='utf-8') as f:
        js = f.read()

    bundled = html.replace('<link rel="stylesheet" href="css/style.css">', f'<style>\n{css}\n</style>')
    bundled = bundled.replace('<script src="js/app.js"></script>', f'<script>\n{js}\n</script>')

    raw_bytes = bundled.encode('utf-8')
    # Generate C byte array
    hex_bytes = [f'0x{b:02x}' for b in raw_bytes]
    # Group in lines of 20
    lines = []
    for i in range(0, len(hex_bytes), 20):
        lines.append('    ' + ', '.join(hex_bytes[i:i+20]))
    byte_array_body = ',\n'.join(lines)

    header_content = f'''#pragma once
#include <string>

// Embedded Bundled UI (HTML + CSS + JS) generated automatically
inline const unsigned char g_embedded_index_html_data[] = {{
{byte_array_body}
}};

inline std::string get_embedded_html() {{
    return std::string(reinterpret_cast<const char*>(g_embedded_index_html_data), sizeof(g_embedded_index_html_data));
}}
'''

    with open('src/embedded_html.hpp', 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"Generated src/embedded_html.hpp with {len(raw_bytes)} bytes of embedded HTML")

if __name__ == '__main__':
    bundle()
