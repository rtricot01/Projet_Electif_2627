#ifndef LISTEJOUEURS_H_INCLUDED
#define LISTEJOUEURS_H_INCLUDED

#include <string>
#include <vector>

#include "Utilisateur.h"

// Lit la liste des joueurs du club depuis un fichier CSV enregistre avec Excel
// ("Fichier > Enregistrer sous > CSV (separateur : point-virgule)" ou "CSV UTF-8").
//
// La premiere ligne doit contenir les titres des colonnes. Sont reconnues
// (majuscules et accents ignores) :
//   - "Licence" / "License" / "N licence"   -> numero de licence (obligatoire)
//   - "Nom"                                 -> nom (obligatoire)
//   - "Prenom"                              -> prenom
//   - "Filiere" / "Departement" / "Spe"     -> filiere
//   - "Type" / "Pratique" / "Categorie"     -> loisir ou competition (valeurs contenant
//                                              "lois" ou "comp", ex : "Loisir", "Compétition")
// Les autres colonnes sont ignorees.
class ListeJoueurs {
public:
    struct Lecture {
        std::vector<JoueurImporte> joueurs;
        std::vector<std::string> avertissements; // lignes ignorees, doublons...
    };

    // Leve DonneeInvalide si le fichier est illisible ou s'il manque une colonne obligatoire.
    static Lecture lireFichierCsv(const std::string& cheminFichier);

    // Meme chose a partir du contenu du fichier (utile pour les tests).
    static Lecture lireTexteCsv(std::string contenu);
};

#endif // LISTEJOUEURS_H_INCLUDED
