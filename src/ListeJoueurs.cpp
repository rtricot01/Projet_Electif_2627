#include "ListeJoueurs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

using namespace std;

namespace {

// ------------------------------------------------------------ encodage du texte

bool estUtf8Valide(const string& texte)
{
    size_t i = 0;
    while (i < texte.size()) {
        const unsigned char c = static_cast<unsigned char>(texte[i]);
        size_t suite = 0;
        if (c < 0x80) {
            suite = 0;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
            suite = 1;
        } else if ((c & 0xF0) == 0xE0) {
            suite = 2;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
            suite = 3;
        } else {
            return false;
        }
        if (i + suite >= texte.size() && suite > 0) {
            return false; // caractere coupe a la fin du fichier
        }
        for (size_t k = 1; k <= suite; ++k) {
            if ((static_cast<unsigned char>(texte[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += suite + 1;
    }
    return true;
}

void ajouterUtf8(string& sortie, unsigned int codePoint)
{
    if (codePoint < 0x80) {
        sortie += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        sortie += static_cast<char>(0xC0 | (codePoint >> 6));
        sortie += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        sortie += static_cast<char>(0xE0 | (codePoint >> 12));
        sortie += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        sortie += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

// Excel sous Windows enregistre souvent les CSV en "Windows-1252" (et pas en UTF-8) :
// on convertit pour que les accents (e accent aigu, c cedille...) soient conserves.
string windows1252VersUtf8(const string& texte)
{
    // Caracteres 0x80 a 0x9F de Windows-1252 (les autres sont identiques a Latin-1).
    static const unsigned int TABLE_80_9F[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

    string sortie;
    sortie.reserve(texte.size() + texte.size() / 8);
    for (const char caractere : texte) {
        const unsigned char c = static_cast<unsigned char>(caractere);
        if (c < 0x80) {
            sortie += caractere;
        } else if (c < 0xA0) {
            ajouterUtf8(sortie, TABLE_80_9F[c - 0x80]);
        } else {
            ajouterUtf8(sortie, c);
        }
    }
    return sortie;
}

// ------------------------------------------------------------ lecture du CSV

string nettoyer(const string& texte)
{
    const size_t debut = texte.find_first_not_of(" \t\r\n");
    if (debut == string::npos) {
        return "";
    }
    const size_t fin = texte.find_last_not_of(" \t\r\n");
    return texte.substr(debut, fin - debut + 1);
}

// Decoupe le fichier en lignes et en cellules, en respectant les guillemets ("Dupont; Jean").
vector<vector<string>> decouper(const string& contenu, char separateur)
{
    vector<vector<string>> lignes(1);
    string cellule;
    bool entreGuillemets = false;

    for (size_t i = 0; i < contenu.size(); ++i) {
        const char c = contenu[i];
        if (entreGuillemets) {
            if (c == '"' && i + 1 < contenu.size() && contenu[i + 1] == '"') {
                cellule += '"';
                ++i;
            } else if (c == '"') {
                entreGuillemets = false;
            } else {
                cellule += c;
            }
        } else if (c == '"') {
            entreGuillemets = true;
        } else if (c == separateur) {
            lignes.back().push_back(nettoyer(cellule));
            cellule.clear();
        } else if (c == '\n') {
            lignes.back().push_back(nettoyer(cellule));
            cellule.clear();
            lignes.emplace_back();
        } else if (c != '\r') {
            cellule += c;
        }
    }
    lignes.back().push_back(nettoyer(cellule));
    return lignes;
}

char devinerSeparateur(const string& premiereLigne)
{
    const auto nb = [&](char c) { return count(premiereLigne.begin(), premiereLigne.end(), c); };
    if (nb(';') >= nb(',') && nb(';') >= nb('\t')) {
        return ';';
    }
    return nb('\t') > nb(',') ? '\t' : ',';
}

// "N° Licence" -> "nlicence", "Prénom" -> "prenom" : pour reconnaitre les titres de colonnes.
string normaliserTitre(const string& titre)
{
    static const pair<const char*, char> ACCENTS[] = {
        {"\xC3\xA9", 'e'}, {"\xC3\xA8", 'e'}, {"\xC3\xAA", 'e'}, {"\xC3\xAB", 'e'},
        {"\xC3\x89", 'e'}, {"\xC3\x88", 'e'}, {"\xC3\xA0", 'a'}, {"\xC3\xA2", 'a'},
        {"\xC3\xAE", 'i'}, {"\xC3\xAF", 'i'}, {"\xC3\xB4", 'o'}, {"\xC3\xBB", 'u'},
        {"\xC3\xB9", 'u'}, {"\xC3\xA7", 'c'}};

    string texte = titre;
    for (const auto& [accent, lettre] : ACCENTS) {
        size_t position;
        while ((position = texte.find(accent)) != string::npos) {
            texte.replace(position, 2, 1, lettre);
        }
    }
    string resultat;
    for (const char c : texte) {
        if (isalnum(static_cast<unsigned char>(c))) {
            resultat += static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
    }
    return resultat;
}

struct Colonnes {
    optional<size_t> licence, nom, prenom, filiere, type;
};

Colonnes reconnaitreColonnes(const vector<string>& titres)
{
    Colonnes colonnes;
    for (size_t i = 0; i < titres.size(); ++i) {
        const string t = normaliserTitre(titres[i]);
        if (!colonnes.licence && (t.find("licen") != string::npos)) {
            colonnes.licence = i;
        } else if (!colonnes.prenom && t.find("prenom") != string::npos) {
            colonnes.prenom = i;
        } else if (!colonnes.nom && (t == "nom" || t == "noms" || t == "nomdefamille" || t == "nomdusage")) {
            colonnes.nom = i;
        } else if (!colonnes.filiere
                   && (t.find("filiere") != string::npos || t.find("departement") != string::npos
                       || t == "spe" || t.find("specialite") != string::npos)) {
            colonnes.filiere = i;
        } else if (!colonnes.type
                   && (t == "type" || t.find("typede") == 0 || t.find("pratique") != string::npos
                       || t.find("categorie") != string::npos || t == "loisircompetition"
                       || t == "competitionloisir")) {
            colonnes.type = i;
        }
    }
    return colonnes;
}

string cellule(const vector<string>& ligne, optional<size_t> colonne)
{
    return (colonne && *colonne < ligne.size()) ? ligne[*colonne] : "";
}

// Excel peut transformer une licence numerique en "1234567.0" ou ajouter des espaces.
string normaliserLicence(string licence)
{
    licence.erase(remove_if(licence.begin(), licence.end(),
                            [](unsigned char c) { return isspace(c); }), licence.end());
    if (licence.size() > 2 && licence.compare(licence.size() - 2, 2, ".0") == 0
        && all_of(licence.begin(), licence.end() - 2, [](unsigned char c) { return isdigit(c); })) {
        licence.resize(licence.size() - 2);
    }
    return licence;
}

} // namespace

ListeJoueurs::Lecture ListeJoueurs::lireFichierCsv(const string& cheminFichier)
{
    ifstream fichier(cheminFichier, ios::binary);
    if (!fichier) {
        throw DonneeInvalide("Impossible d'ouvrir le fichier \"" + cheminFichier + "\".");
    }
    return lireTexteCsv(string((istreambuf_iterator<char>(fichier)), istreambuf_iterator<char>()));
}

ListeJoueurs::Lecture ListeJoueurs::lireTexteCsv(string contenu)
{
    // Marque "UTF-8 avec BOM" ajoutee par Excel au debut du fichier.
    if (contenu.rfind("\xEF\xBB\xBF", 0) == 0) {
        contenu.erase(0, 3);
    }
    if (!estUtf8Valide(contenu)) {
        contenu = windows1252VersUtf8(contenu);
    }

    const string premiereLigne = contenu.substr(0, contenu.find('\n'));
    const vector<vector<string>> lignes = decouper(contenu, devinerSeparateur(premiereLigne));

    const Colonnes colonnes = reconnaitreColonnes(lignes.front());
    if (!colonnes.licence || !colonnes.nom) {
        throw DonneeInvalide(
            "La premiere ligne du fichier doit contenir au moins les colonnes \"Nom\" et \"Licence\" "
            "(colonnes trouvees : " + premiereLigne + ").");
    }

    Lecture lecture;
    set<string> licencesVues;
    for (size_t i = 1; i < lignes.size(); ++i) {
        const vector<string>& ligne = lignes[i];
        const bool vide = all_of(ligne.begin(), ligne.end(), [](const string& c) { return c.empty(); });
        if (vide) {
            continue;
        }

        JoueurImporte joueur;
        joueur.numeroLicence = normaliserLicence(cellule(ligne, colonnes.licence));
        joueur.nom = cellule(ligne, colonnes.nom);
        joueur.prenom = cellule(ligne, colonnes.prenom);
        joueur.filiere = cellule(ligne, colonnes.filiere);

        const string type = normaliserTitre(cellule(ligne, colonnes.type));
        if (type.find("comp") != string::npos) {
            joueur.typeJoueur = "competition";
        } else if (type.find("lois") != string::npos) {
            joueur.typeJoueur = "loisir";
        } else if (!type.empty()) {
            lecture.avertissements.push_back("Ligne " + to_string(i + 1) + " : type \""
                + cellule(ligne, colonnes.type) + "\" non reconnu (attendu : loisir ou competition), ignore.");
        }

        const string numeroLigne = "Ligne " + to_string(i + 1);
        if (joueur.numeroLicence.empty() || joueur.nom.empty()) {
            lecture.avertissements.push_back(numeroLigne + " ignoree : nom ou licence manquant.");
            continue;
        }
        if (!licencesVues.insert(joueur.numeroLicence).second) {
            lecture.avertissements.push_back(
                numeroLigne + " ignoree : la licence " + joueur.numeroLicence + " apparait deja plus haut.");
            continue;
        }
        lecture.joueurs.push_back(joueur);
    }
    return lecture;
}
