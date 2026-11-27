#include "Session.h"

#include <pqxx/pqxx>
#include <utility>

#include "Db.h"

using namespace std;

SessionManager::SessionManager(string connexion) : Manager(std::move(connexion)) {}

string SessionManager::ouvrir(int utilisateurId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    // Menage : on en profite pour effacer les sessions expirees.
    tx.exec("DELETE FROM sessions WHERE expire_le < now();");

    const string jeton = tx.exec("SELECT encode(gen_random_bytes(32), 'hex');")[0][0].as<string>();
    db::exec(tx,
        "INSERT INTO sessions (empreinte, utilisateur_id, expire_le) "
        "VALUES (encode(digest($1, 'sha256'), 'hex'), $2, now() + make_interval(days => $3));",
        jeton, utilisateurId, DUREE_JOURS);
    tx.commit();
    return jeton;
}

optional<Utilisateur> SessionManager::utilisateur(const string& jeton)
{
    if (jeton.empty()) {
        return nullopt;
    }
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx,
        "SELECT utilisateur_id FROM sessions "
        "WHERE empreinte = encode(digest($1, 'sha256'), 'hex') AND expire_le > now();",
        jeton);
    if (res.empty()) {
        return nullopt;
    }
    return UtilisateurManager(m_connexion).parId(res[0][0].as<int>());
}

void SessionManager::fermer(const string& jeton)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    db::exec(tx, "DELETE FROM sessions WHERE empreinte = encode(digest($1, 'sha256'), 'hex');", jeton);
    tx.commit();
}
