#include "Branding.hpp"
#include "BrandingData.hpp"
#include <wx/iconbndl.h>
#include <wx/imagpng.h>
#include <wx/mstream.h>
#include <wx/toplevel.h>
#include <stdexcept>

namespace {
wxImage loadLogo() {
    wxMemoryInputStream stream(unballLogoPng, sizeof(unballLogoPng));
    wxPNGHandler decoder;
    wxImage image;
    if (!decoder.LoadFile(&image, stream)) throw std::runtime_error("Não foi possível carregar a logo da UnBall.");
    return image;
}
}

wxBitmapBundle unballLogo() {
    const auto image = loadLogo();
    return wxBitmapBundle::FromBitmaps(wxBitmap(image.Scale(40, 40, wxIMAGE_QUALITY_HIGH)),
                                      wxBitmap(image.Scale(80, 80, wxIMAGE_QUALITY_HIGH)));
}

void setUnballWindowIcon(wxTopLevelWindow& window) {
    const auto image = loadLogo();
    wxIconBundle icons;
    for (const int size : {16, 32, 48, 64, 128, 256}) {
        wxIcon icon;
        icon.CopyFromBitmap(wxBitmap(image.Scale(size, size, wxIMAGE_QUALITY_HIGH)));
        icons.AddIcon(icon);
    }
    window.SetIcons(icons);
}
