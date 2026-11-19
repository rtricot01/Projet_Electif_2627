#include <iostream>
#include <string>
#include "Init.h"
#include "User.h"

using namespace std;

int main()
{
    cout << "[1/3] Demarrage du programme...\n";
    string conn_str = "postgresql://neondb_owner:npg_njEle7rF9BVH@ep-rapid-dew-b9l28pba-pooler.c-7.eu-central-1.aws.neon.tech/neondb?sslmode=require&channel_binding=require";

    try
    {
        cout << "[2/3] Tentative de connexion a Neon...\n";
        Init dbInit(conn_str);

        cout << "[3/3] Creation de la table...\n";
        dbInit.initTable();

        UserManager manager(conn_str);
        cout << "Connexion etablie avec succes !\n";

        int choice = 0;
        while (true)
        {
            cout << "\n--- MENU ---\n";
            cout << "1. S'inscrire\n";
            cout << "2. Se connecter\n";
            cout << "3. Quitter\n";
            cout << "Choix : ";
            if (!(cin >> choice))
            {
                break;
            }

            if (choice == 1)
            {
                string user, pass;
                cout << "Nom d'utilisateur : ";
                cin >> user;
                cout << "Mot de passe : ";
                cin >> pass;

                if (manager.registerUser(user, pass))
                {
                    cout << "Compte cree avec succes !\n";
                }
                else
                {
                    cout << "Echec : nom d'utilisateur deja pris ou erreur BDD.\n";
                }
            }
            else if (choice == 2)
            {
                string user, pass;
                cout << "Nom d'utilisateur : ";
                cin >> user;
                cout << "Mot de passe : ";
                cin >> pass;

                if (manager.authenticate(user, pass))
                {
                    cout << "\nConnexion reussie ! Bienvenue " << user << ".\n";
                    cout << "\n--- Liste des membres inscrits ---\n";
                    vector<User> users = manager.getAllUsers();
                    for (const auto &u : users)
                    {
                        cout << "- ID " << u.id << " : " << u.username << '\n';
                    }
                }
                else
                {
                    cout << "Identifiants invalides.\n";
                }
            }
            else if (choice == 3)
            {
                break;
            }
        }
    }
    catch (const exception &e)
    {
        cerr << "Erreur critique : " << e.what() << '\n';
        return 1;
    }

    return 0;
}
