#ifndef QRCODE_H_INCLUDED
#define QRCODE_H_INCLUDED

#include <ostream>
#include <string>

// Fabrique l'image d'un QR code (dans la console ou dans un fichier SVG).
// Utilise la bibliotheque "qrcodegen" (Nayuki) si elle est installee.
class QrCode {
public:
    // Vrai si la bibliotheque de QR codes est disponible a la compilation.
    static bool disponible();

    // Dessine le QR code dans la console (fond sombre conseille).
    static void afficher(const std::string& texte, std::ostream& sortie);

    // Renvoie l'image SVG du QR code (vide si la bibliotheque est absente). Utilise par le site web.
    static std::string svg(const std::string& texte);

    // Enregistre le QR code dans un fichier .svg (imprimable, ou a ouvrir sur le telephone).
    static bool exporterSvg(const std::string& texte, const std::string& cheminFichier);
};

#endif // QRCODE_H_INCLUDED
