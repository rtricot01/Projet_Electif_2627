// Point d'entree du site web (executable "site").
//
// Variables d'environnement :
//   DATABASE_URL : adresse de la base PostgreSQL (obligatoire)
//   PORT         : port d'ecoute (fourni automatiquement par Railway, 8080 par defaut)
//   WEB_DIR      : dossier des pages HTML/CSS/JS ("web" par defaut)

#include <cstdlib>
#include <iostream>
#include <string>

#include "Db.h"
#include "Init.h"
#include "ServeurWeb.h"

using namespace std;

namespace {
string variable(const char* nom, const string& defaut)
{
    const char* valeur = getenv(nom);
    return (valeur != nullptr && *valeur != '\0') ? valeur : defaut;
}
} // namespace

int main()
{
    // Affiche les messages immediatement (sinon ils restent en memoire et n'apparaissent
    // pas dans les journaux de Railway).
    cout << unitbuf;

    try {
        const string connexion = db::connectionStringFromEnv();
        Init(connexion).initTables();

        const int port = stoi(variable("PORT", "8080"));
        ServeurWeb serveur(connexion, variable("WEB_DIR", "web"));
        if (!serveur.demarrer("0.0.0.0", port)) {
            cerr << "Impossible d'ecouter sur le port " << port << '\n';
            return 1;
        }
    } catch (const exception& e) {
        cerr << "Erreur critique : " << e.what() << '\n';
        return 1;
    }
    return 0;
}
