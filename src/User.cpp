#include "User.h"
#include <iostream>
#include <pqxx/pqxx>
#include <utility>
#include "Db.h"

using namespace std;
using namespace pqxx;

// Les parametres portent le meme nom que les attributs : il faut donc
// utiliser la liste d'initialisation (sinon "id = id;" ne fait rien).
User::User(int id, string username, string password, string nom, string prenom)
    : id(id),
      username(std::move(username)),
      password(std::move(password)),
      nom(std::move(nom)),
      prenom(std::move(prenom))
{
}

UserManager::UserManager(const string& conn_str){
    m_connection_string = conn_str;
}

bool UserManager::registerUser(const string& username, const string& password) {
    try {
        connection conn(m_connection_string);
        work tx(conn);

        // Le mot de passe est hache (bcrypt) par PostgreSQL : il n'est jamais stocke en clair.
        db::exec(tx,
            "INSERT INTO utilisateurs (username, password) VALUES ($1, crypt($2, gen_salt('bf')));",
            username, password
        );

        tx.commit();
        return true;
    } catch (const pqxx::sql_error& e) {
        cerr << "Erreur SQL (inscription) : " << e.what() << '\n';
        return false;
    }
}

bool UserManager::authenticate(const string& username, const string& password) {
    try {
        pqxx::connection conn(m_connection_string);
        pqxx::work tx(conn);

        // crypt(motDePasseSaisi, hashStocke) redonne le hash stocke si le mot de passe est bon.
        pqxx::result res = db::exec(tx,
            "SELECT id FROM utilisateurs WHERE username = $1 AND password = crypt($2, password);",
            username, password
        );

        return !res.empty();
    } catch (const sql_error& e) {
        cerr << "Erreur SQL (authentification) : " << e.what() << '\n';
        return false;
    }
}

std::vector<User> UserManager::getAllUsers() {
    vector<User> users;
    try {
        connection conn(m_connection_string);
        work tx(conn);

        result res = tx.exec("SELECT id, username FROM utilisateurs ORDER BY id ASC;");

        for (const auto& row : res) {
            User u;
            u.id = row["id"].as<int>();
            u.username = row["username"].as<string>();
            users.push_back(u);
        }
    } catch (const sql_error& e) {
        cerr << "Erreur SQL (getAllUsers) : " << e.what() << '\n';
    }
    return users;
}
