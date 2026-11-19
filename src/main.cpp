#include <iostream>
#include <string>

#include "Application.h"
#include "Db.h"
#include "Init.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

int main()
{
#ifdef _WIN32
    // Pour que la console Windows affiche correctement le QR code.
    SetConsoleOutputCP(CP_UTF8);
#endif

    cout << "[1/3] Demarrage du programme...\n";

    try
    {
        // L'adresse de la base (avec son mot de passe) vient de la variable
        // d'environnement DATABASE_URL, elle n'est pas ecrite dans le code.
        string conn_str = db::connectionStringFromEnv();

        cout << "[2/3] Tentative de connexion a la base...\n";
        Init dbInit(conn_str);

        cout << "[3/3] Creation des tables...\n";
        dbInit.initTables();
        cout << "Connexion etablie avec succes !\n";

        Application application(conn_str);
        application.lancer();
    }
    catch (const exception &e)
    {
        cerr << "Erreur critique : " << e.what() << '\n';
        return 1;
    }

    return 0;
}
