#include "Init.h"
#include <iostream>
#include <pqxx/pqxx>

using namespace std ;
using namespace pqxx;


Init::Init(const string& conn_str){
    m_connection_string = conn_str;
}

string Init::getConnectionString() const {
    return m_connection_string;
}

// Toutes les requetes utilisent "IF NOT EXISTS" : on peut relancer le
// programme autant de fois qu'on veut sans perdre les donnees.
void Init::initTables() {
    try {
        connection conn(m_connection_string);
        work tx(conn);

        // pgcrypto fournit crypt()/gen_salt() (mots de passe) et gen_random_bytes() (QR codes).
        tx.exec("CREATE EXTENSION IF NOT EXISTS pgcrypto;");

        // ----- Utilisateurs
        tx.exec(
            "CREATE TABLE IF NOT EXISTS utilisateurs ("
            "  id SERIAL PRIMARY KEY,"
            "  numero_licence VARCHAR(50) UNIQUE NOT NULL,"
            "  password VARCHAR(100) NOT NULL"
            ");"
        );
        // Ancienne version de la table : l'identifiant s'appelait "username".
        tx.exec(
            "DO $$ BEGIN"
            "  IF EXISTS (SELECT 1 FROM information_schema.columns"
            "             WHERE table_name = 'utilisateurs' AND column_name = 'username') THEN"
            "    ALTER TABLE utilisateurs RENAME COLUMN username TO numero_licence;"
            "  END IF;"
            "END $$;"
        );
        tx.exec(
            "ALTER TABLE utilisateurs"
            "  ADD COLUMN IF NOT EXISTS nom VARCHAR(100) NOT NULL DEFAULT '',"
            "  ADD COLUMN IF NOT EXISTS prenom VARCHAR(100) NOT NULL DEFAULT '',"
            "  ADD COLUMN IF NOT EXISTS filiere VARCHAR(100) NOT NULL DEFAULT '',"
            "  ADD COLUMN IF NOT EXISTS statut VARCHAR(10) NOT NULL DEFAULT 'client'"
            "      CHECK (statut IN ('client', 'admin')),"
            "  ADD COLUMN IF NOT EXISTS qr_token VARCHAR(64) UNIQUE NOT NULL"
            "      DEFAULT encode(gen_random_bytes(16), 'hex');"
        );
        // Mot de passe vide (NULL) = joueur importe depuis la liste du club,
        // qui n'a pas encore fait sa premiere connexion.
        tx.exec("ALTER TABLE utilisateurs ALTER COLUMN password DROP NOT NULL;");

        // ----- Equipes
        tx.exec(
            "CREATE TABLE IF NOT EXISTS equipes ("
            "  id SERIAL PRIMARY KEY,"
            "  nom VARCHAR(100) UNIQUE NOT NULL,"
            "  photo BYTEA"
            ");"
        );
        tx.exec(
            "CREATE TABLE IF NOT EXISTS equipe_membres ("
            "  equipe_id INT NOT NULL REFERENCES equipes(id) ON DELETE CASCADE,"
            "  utilisateur_id INT NOT NULL REFERENCES utilisateurs(id) ON DELETE CASCADE,"
            "  PRIMARY KEY (equipe_id, utilisateur_id)"
            ");"
        );

        // ----- Entrainements et presences
        tx.exec(
            "CREATE TABLE IF NOT EXISTS entrainements ("
            "  id SERIAL PRIMARY KEY,"
            "  date_heure TIMESTAMP NOT NULL,"
            "  lieu VARCHAR(150) NOT NULL,"
            "  equipe_id INT REFERENCES equipes(id) ON DELETE SET NULL,"
            "  description TEXT NOT NULL DEFAULT ''"
            ");"
        );
        tx.exec(
            "CREATE TABLE IF NOT EXISTS entrainement_inscriptions ("
            "  entrainement_id INT NOT NULL REFERENCES entrainements(id) ON DELETE CASCADE,"
            "  utilisateur_id INT NOT NULL REFERENCES utilisateurs(id) ON DELETE CASCADE,"
            "  PRIMARY KEY (entrainement_id, utilisateur_id)"
            ");"
        );
        tx.exec(
            "CREATE TABLE IF NOT EXISTS presences ("
            "  entrainement_id INT NOT NULL REFERENCES entrainements(id) ON DELETE CASCADE,"
            "  utilisateur_id INT NOT NULL REFERENCES utilisateurs(id) ON DELETE CASCADE,"
            "  scanne_le TIMESTAMP NOT NULL DEFAULT now(),"
            "  scanne_par INT REFERENCES utilisateurs(id) ON DELETE SET NULL,"
            "  PRIMARY KEY (entrainement_id, utilisateur_id)"
            ");"
        );

        // ----- Matchs
        tx.exec(
            "CREATE TABLE IF NOT EXISTS matchs ("
            "  id SERIAL PRIMARY KEY,"
            "  date_heure TIMESTAMP NOT NULL,"
            "  lieu VARCHAR(150) NOT NULL,"
            "  equipe_id INT NOT NULL REFERENCES equipes(id) ON DELETE CASCADE,"
            "  adversaire VARCHAR(100) NOT NULL,"
            "  type_competition VARCHAR(10) NOT NULL CHECK (type_competition IN ('CFE', 'ACAD')),"
            "  description TEXT NOT NULL DEFAULT ''"
            ");"
        );
        tx.exec(
            "CREATE TABLE IF NOT EXISTS match_sets ("
            "  match_id INT NOT NULL REFERENCES matchs(id) ON DELETE CASCADE,"
            "  numero INT NOT NULL CHECK (numero BETWEEN 1 AND 5),"
            "  points_equipe INT NOT NULL CHECK (points_equipe >= 0),"
            "  points_adversaire INT NOT NULL CHECK (points_adversaire >= 0),"
            "  PRIMARY KEY (match_id, numero)"
            ");"
        );

        tx.commit();
        cout << "[BDD] Tables pretes.\n";
    } catch (const sql_error& e) {
        cerr << "Erreur SQL lors de l'init : " << e.what() << '\n';
        throw;
    }
}
