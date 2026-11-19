#include "QrCode.h"

#include <fstream>

#if defined(ASV_AVEC_QRCODEGEN)
#  if __has_include(<qrcodegen.hpp>)
#    include <qrcodegen.hpp>
#  else
#    include <qrcodegen/qrcodegen.hpp>
#  endif
#endif

using namespace std;

#if defined(ASV_AVEC_QRCODEGEN)

namespace {
qrcodegen::QrCode fabriquer(const string& texte)
{
    return qrcodegen::QrCode::encodeText(texte.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
}
} // namespace

bool QrCode::disponible() { return true; }

void QrCode::afficher(const string& texte, ostream& sortie)
{
    const qrcodegen::QrCode qr = fabriquer(texte);
    const int marge = 2;
    // Dans une console a fond sombre, on dessine les cases claires en blanc
    // pour que le telephone voie un QR code "noir sur blanc".
    for (int y = -marge; y < qr.getSize() + marge; ++y) {
        for (int x = -marge; x < qr.getSize() + marge; ++x) {
            sortie << (qr.getModule(x, y) ? "  " : "██");
        }
        sortie << '\n';
    }
}

bool QrCode::exporterSvg(const string& texte, const string& cheminFichier)
{
    const qrcodegen::QrCode qr = fabriquer(texte);
    const int marge = 4;
    const int taille = qr.getSize() + 2 * marge;

    ofstream fichier(cheminFichier);
    if (!fichier) {
        return false;
    }
    fichier << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << taille << ' ' << taille
            << "\" width=\"400\" height=\"400\" shape-rendering=\"crispEdges\">\n"
            << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n<path d=\"";
    for (int y = 0; y < qr.getSize(); ++y) {
        for (int x = 0; x < qr.getSize(); ++x) {
            if (qr.getModule(x, y)) {
                fichier << 'M' << (x + marge) << ',' << (y + marge) << "h1v1h-1z";
            }
        }
    }
    fichier << "\" fill=\"#000000\"/>\n</svg>\n";
    return true;
}

#else // bibliotheque absente : on se contente du texte

bool QrCode::disponible() { return false; }

void QrCode::afficher(const string& texte, ostream& sortie)
{
    sortie << "(Bibliotheque qrcodegen absente : impossible de dessiner le QR code.)\n"
           << "Contenu du QR code : " << texte << '\n';
}

bool QrCode::exporterSvg(const string&, const string&) { return false; }

#endif
