#include "Application.h"

#include <algorithm>
#include <iostream>
#include <pqxx/pqxx>
#include <sstream>

#include "ListeJoueurs.h"
#include "QrCode.h"

using namespace std;

namespace {

// Levee quand l'utilisateur ferme l'entree (Ctrl+Z / Ctrl+D) : on quitte proprement.
struct FinDeSaisie {};

void titre(const string& texte)
{
    cout << "\n=== " << texte << " ===\n";
}

} // namespace

Application::Application(const string& connexion)
    : m_utilisateurs(connexion), m_equipes(connexion), m_entrainements(connexion), m_matchs(connexion)
{
}

// ======================================================================
//  Boucle principale et menus
// ======================================================================

void Application::lancer()
{
    try {
        afficherMenu("AS VOLLEY - ACCUEIL", {
            {"Se connecter (premiere connexion : avec votre numero de licence)", [this] { connexion(); }},
            {"Creer le compte administrateur du club", [this] { creerCompteAdmin(); }, false,
             [this] { return !m_utilisateurs.existeUnAdmin(); }},
        });
    } catch (const FinDeSaisie&) {
        cout << '\n';
    }
    cout << "Au revoir !\n";
}

// Affiche un menu numerote jusqu'a ce que l'utilisateur choisisse "0".
// Les options reservees aux admins sont cachees pour les autres membres.
// Les erreurs (droits, saisie invalide, base de donnees) sont affichees sans quitter le programme.
void Application::afficherMenu(const string& titreMenu, const vector<Option>& options)
{
    while (true) {
        vector<const Option*> visibles;
        for (const Option& option : options) {
            if ((!option.adminSeulement || estAdmin()) && (!option.condition || option.condition())) {
                visibles.push_back(&option);
            }
        }

        titre(titreMenu);
        for (size_t i = 0; i < visibles.size(); ++i) {
            cout << (i + 1) << ". " << visibles[i]->libelle << (visibles[i]->adminSeulement ? "  [admin]" : "") << '\n';
        }
        cout << "0. " << (m_connecte ? "Retour" : "Quitter") << '\n';

        const optional<int> choix = lireEntier("Choix");
        if (choix && *choix == 0) {
            return;
        }
        if (!choix || *choix < 1 || *choix > static_cast<int>(visibles.size())) {
            cout << "Choix invalide.\n";
            continue;
        }

        try {
            visibles[*choix - 1]->action();
        } catch (const AccesRefuse& e) {
            cout << e.what() << '\n';
        } catch (const DonneeInvalide& e) {
            cout << "Erreur : " << e.what() << '\n';
        } catch (const pqxx::broken_connection& e) {
            cout << "Connexion a la base perdue : " << e.what() << '\n';
        } catch (const pqxx::sql_error& e) {
            cout << "Erreur de la base de donnees : " << e.what() << '\n';
        }
    }
}

// Une seule entree pour tout le monde : le programme regarde si la licence
// a deja un mot de passe. Si non, c'est la premiere connexion du joueur.
void Application::connexion()
{
    titre("CONNEXION");
    const string licence = lireTexte("Numero de licence");

    switch (m_utilisateurs.etatCompte(licence)) {
    case EtatCompte::LicenceInconnue:
        cout << "Ce numero de licence n'est pas dans la liste du club. Verifiez-le ou contactez un responsable.\n";
        return;
    case EtatCompte::AActiver:
        premiereConnexion(licence);
        break;
    case EtatCompte::DejaActive:
        m_connecte = m_utilisateurs.connecter(licence, lireTexte("Mot de passe"));
        if (!m_connecte) {
            cout << "Mot de passe incorrect. Si vous l'avez oublie, demandez a un responsable de le reinitialiser.\n";
            return;
        }
        break;
    }
    if (!m_connecte) {
        return;
    }

    cout << "\nConnexion reussie ! Bienvenue " << m_connecte->nomComplet()
         << " (" << statutVersTexte(m_connecte->getStatut()) << ").\n";
    menuPrincipal();
    m_connecte.reset();
    cout << "Vous etes deconnecte.\n";
}

void Application::premiereConnexion(const string& licence)
{
    titre("PREMIERE CONNEXION");
    cout << "Bienvenue ! Choisissez le mot de passe que vous utiliserez pour vos prochaines connexions.\n";
    const string motDePasse = choisirMotDePasse();
    if (motDePasse.empty()) {
        return;
    }
    m_connecte = m_utilisateurs.activerCompte(licence, motDePasse);
    if (!m_connecte) {
        cout << "Ce compte a deja ete active. Connectez-vous avec votre mot de passe.\n";
        return;
    }
    cout << "Compte active.\n";
}

// Demande le mot de passe deux fois. Renvoie "" si les deux saisies sont differentes.
string Application::choisirMotDePasse()
{
    const string motDePasse = lireTexte("Nouveau mot de passe (" +
        to_string(UtilisateurManager::LONGUEUR_MIN_MOT_DE_PASSE) + " caracteres minimum)");
    if (motDePasse.size() < UtilisateurManager::LONGUEUR_MIN_MOT_DE_PASSE) {
        cout << "Mot de passe trop court.\n";
        return "";
    }
    if (lireTexte("Confirmez le mot de passe") != motDePasse) {
        cout << "Les deux mots de passe sont differents.\n";
        return "";
    }
    return motDePasse;
}

// Visible uniquement tant que le club n'a aucun admin (installation du programme).
void Application::creerCompteAdmin()
{
    titre("COMPTE ADMINISTRATEUR");
    const string licence = lireTexte("Numero de licence");
    const string nom = lireTexte("Nom");
    const string prenom = lireTexte("Prenom");
    const string filiere = lireTexte("Filiere a l'INSA");
    if (licence.empty() || nom.empty() || prenom.empty()) {
        throw DonneeInvalide("Licence, nom et prenom sont obligatoires.");
    }
    const string motDePasse = choisirMotDePasse();
    if (motDePasse.empty()) {
        return;
    }
    if (m_utilisateurs.creerPremierAdmin(licence, motDePasse, nom, prenom, filiere)) {
        cout << "Compte administrateur cree. Connectez-vous, puis importez la liste des joueurs "
                "(Membres du club > Importer).\n";
    } else {
        cout << "Echec : ce numero de licence a deja un compte.\n";
    }
}

void Application::menuPrincipal()
{
    afficherMenu("MENU PRINCIPAL", {
        {"Mon profil et mon QR code", [this] {
            afficherMenu("MON PROFIL", {
                {"Voir mon profil", [this] { monProfil(); }},
                {"Afficher / enregistrer mon QR code", [this] { monQrCode(); }},
            });
        }},
        {"Equipes", [this] {
            afficherMenu("EQUIPES", {
                {"Voir les equipes", [this] { afficherEquipes(); }},
                {"Enregistrer la photo d'une equipe dans un fichier", [this] { exporterPhotoEquipe(); }},
                {"Creer une equipe", [this] { creerEquipe(); }, true},
                {"Modifier une equipe (nom et joueurs)", [this] { modifierEquipe(); }, true},
                {"Changer la photo d'une equipe", [this] { changerPhotoEquipe(); }, true},
                {"Supprimer une equipe", [this] { supprimerEquipe(); }, true},
            });
        }},
        {"Entrainements", [this] {
            afficherMenu("ENTRAINEMENTS", {
                {"Voir les entrainements", [this] { afficherEntrainements(); }},
                {"M'inscrire a un entrainement", [this] { sInscrireEntrainement(); }},
                {"Me desinscrire d'un entrainement", [this] { seDesinscrireEntrainement(); }},
                {"Scanner les QR codes (valider les presences)", [this] { scannerPresences(); }, true},
                {"Voir les presences d'une seance", [this] { voirPresences(); }, true},
                {"Creer un entrainement", [this] { creerEntrainement(); }, true},
                {"Modifier un entrainement", [this] { modifierEntrainement(); }, true},
                {"Supprimer un entrainement", [this] { supprimerEntrainement(); }, true},
            });
        }},
        {"Matchs", [this] {
            afficherMenu("MATCHS", {
                {"Voir les matchs", [this] { afficherMatchs(); }},
                {"Creer un match", [this] { creerMatch(); }, true},
                {"Modifier un match", [this] { modifierMatch(); }, true},
                {"Saisir le score d'un match", [this] { saisirScore(); }, true},
                {"Supprimer un match", [this] { supprimerMatch(); }, true},
            });
        }},
        {"Membres du club", [this] {
            afficherMenu("MEMBRES", {
                {"Voir les membres", [this] { afficherMembres(); }},
                {"Importer la liste des joueurs (fichier CSV enregistre depuis Excel)", [this] { importerJoueurs(); }},
                {"Reinitialiser le mot de passe d'un membre", [this] { reinitialiserMotDePasse(); }},
                {"Passer un membre admin / client", [this] { changerStatutMembre(); }},
                {"Supprimer un compte", [this] { supprimerMembre(); }},
            });
        }, true},
    });
}

void Application::rechargerUtilisateurConnecte()
{
    if (m_connecte) {
        m_connecte = m_utilisateurs.parId(m_connecte->getId());
    }
}

// ======================================================================
//  Profil
// ======================================================================

void Application::monProfil()
{
    rechargerUtilisateurConnecte();
    if (!m_connecte) {
        return;
    }
    titre("MON PROFIL");
    cout << "Nom       : " << m_connecte->getNom() << '\n'
         << "Prenom    : " << m_connecte->getPrenom() << '\n'
         << "Filiere   : " << m_connecte->getFiliere() << '\n'
         << "Licence   : " << m_connecte->getNumeroLicence() << '\n'
         << "Statut    : " << statutVersTexte(m_connecte->getStatut()) << '\n';
}

void Application::monQrCode()
{
    titre("MON QR CODE");
    cout << "Montrez ce QR code au responsable au debut de chaque entrainement.\n\n";
    QrCode::afficher(m_connecte->contenuQrCode(), cout);

    if (QrCode::disponible() && confirmer("Enregistrer aussi le QR code dans un fichier SVG ?")) {
        const string chemin = "qrcode_" + m_connecte->getNumeroLicence() + ".svg";
        if (QrCode::exporterSvg(m_connecte->contenuQrCode(), chemin)) {
            cout << "QR code enregistre dans " << chemin << '\n';
        } else {
            cout << "Impossible d'ecrire le fichier " << chemin << '\n';
        }
    }
}

// ======================================================================
//  Membres (admin)
// ======================================================================

void Application::afficherMembres()
{
    titre("MEMBRES DU CLUB");
    for (const Utilisateur& u : m_utilisateurs.tous()) {
        cout << "[" << u.getId() << "] " << u.nomComplet()
             << " - licence " << u.getNumeroLicence()
             << " - " << (u.getFiliere().empty() ? "filiere ?" : u.getFiliere())
             << (u.estAdmin() ? " - ADMIN" : "")
             << (u.compteActive() ? "" : " - compte pas encore active") << '\n';
    }
}

void Application::importerJoueurs()
{
    titre("IMPORTER LA LISTE DES JOUEURS");
    cout << "Dans Excel : Fichier > Enregistrer sous > \"CSV UTF-8\" (ou \"CSV (separateur : point-virgule)\").\n"
         << "La premiere ligne doit contenir les titres des colonnes, au moins \"Nom\" et \"Licence\"\n"
         << "(\"Prenom\" et \"Filiere\" sont aussi reconnus).\n";
    const string chemin = lireTexte("Chemin du fichier CSV");
    const ListeJoueurs::Lecture lecture = ListeJoueurs::lireFichierCsv(chemin);

    for (const string& avertissement : lecture.avertissements) {
        cout << "  ! " << avertissement << '\n';
    }
    if (lecture.joueurs.empty()) {
        cout << "Aucun joueur trouve dans le fichier.\n";
        return;
    }

    cout << lecture.joueurs.size() << " joueur(s) trouve(s), par exemple :\n";
    for (size_t i = 0; i < lecture.joueurs.size() && i < 3; ++i) {
        const JoueurImporte& j = lecture.joueurs[i];
        cout << "  - " << j.prenom << " " << j.nom << " (licence " << j.numeroLicence
             << (j.filiere.empty() ? "" : ", " + j.filiere) << ")\n";
    }
    if (!confirmer("Importer ces joueurs ?")) {
        cout << "Import annule.\n";
        return;
    }

    const ResultatImport resultat = m_utilisateurs.importerJoueurs(*m_connecte, lecture.joueurs);
    cout << resultat.ajoutes << " joueur(s) ajoute(s), " << resultat.misAJour << " deja present(s) mis a jour.\n"
         << "Les nouveaux joueurs se connectent avec leur numero de licence et choisissent leur mot de passe.\n";
}

void Application::reinitialiserMotDePasse()
{
    afficherMembres();
    const optional<int> id = lireEntier("Numero du membre");
    if (!id) {
        return;
    }
    if (confirmer("Effacer le mot de passe de " + nomJoueur(*id) + " ?")) {
        m_utilisateurs.reinitialiserMotDePasse(*m_connecte, *id);
        cout << "Mot de passe efface : a sa prochaine connexion, ce membre en choisira un nouveau.\n";
    }
}

void Application::changerStatutMembre()
{
    afficherMembres();
    const optional<int> id = lireEntier("Numero du membre");
    if (!id) {
        return;
    }
    const bool admin = confirmer("Donner les droits admin a ce membre ? (non = le passer en client)");
    m_utilisateurs.changerStatut(*m_connecte, *id, admin ? Statut::Admin : Statut::Client);
    cout << "Statut mis a jour.\n";
}

void Application::supprimerMembre()
{
    afficherMembres();
    const optional<int> id = lireEntier("Numero du membre a supprimer");
    if (!id) {
        return;
    }
    if (confirmer("Supprimer definitivement le compte de " + nomJoueur(*id) + " ?")) {
        m_utilisateurs.supprimer(*m_connecte, *id);
        cout << "Compte supprime.\n";
    }
}

// ======================================================================
//  Equipes
// ======================================================================

void Application::afficherEquipes()
{
    titre("EQUIPES");
    const vector<Equipe> equipes = m_equipes.toutes();
    if (equipes.empty()) {
        cout << "Aucune equipe pour le moment.\n";
    }
    for (const Equipe& equipe : equipes) {
        cout << "[" << equipe.getId() << "] " << equipe.getNom()
             << " (" << equipe.getMembres().size() << " joueurs"
             << (equipe.aUnePhoto() ? ", avec photo" : "") << ")\n";
        for (int joueurId : equipe.getMembres()) {
            cout << "     - " << nomJoueur(joueurId) << '\n';
        }
    }
}

void Application::creerEquipe()
{
    titre("CREER UNE EQUIPE");
    const string nom = lireTexte("Nom de l'equipe");
    const vector<int> joueurs = choisirJoueurs({});
    const Equipe equipe = m_equipes.creer(*m_connecte, nom, joueurs);
    cout << "Equipe \"" << equipe.getNom() << "\" creee avec " << equipe.getMembres().size() << " joueurs.\n";
}

void Application::modifierEquipe()
{
    const optional<int> id = choisirEquipe(false);
    if (!id) {
        return;
    }
    const optional<Equipe> equipe = m_equipes.parId(*id);
    if (!equipe) {
        throw DonneeInvalide("Cette equipe n'existe pas.");
    }
    const string nom = lireTexte("Nom de l'equipe", equipe->getNom());
    const vector<int> joueurs = choisirJoueurs(equipe->getMembres());
    m_equipes.modifier(*m_connecte, *id, nom, joueurs);
    cout << "Equipe modifiee.\n";
}

void Application::supprimerEquipe()
{
    const optional<int> id = choisirEquipe(false);
    if (!id) {
        return;
    }
    if (confirmer("Supprimer l'equipe \"" + nomEquipe(*id) + "\" et tous ses matchs ?")) {
        m_equipes.supprimer(*m_connecte, *id);
        cout << "Equipe supprimee.\n";
    } else {
        cout << "Suppression annulee.\n";
    }
}

void Application::changerPhotoEquipe()
{
    const optional<int> id = choisirEquipe(false);
    if (!id) {
        return;
    }
    const string chemin = lireTexte("Chemin du fichier image (jpg, png...)");
    m_equipes.definirPhoto(*m_connecte, *id, chemin);
    cout << "Photo enregistree dans la base.\n";
}

void Application::exporterPhotoEquipe()
{
    const optional<int> id = choisirEquipe(false);
    if (!id) {
        return;
    }
    const string chemin = lireTexte("Nom du fichier a creer (ex : equipe.jpg)");
    if (m_equipes.exporterPhoto(*id, chemin)) {
        cout << "Photo enregistree dans " << chemin << '\n';
    } else {
        cout << "Cette equipe n'a pas de photo.\n";
    }
}

// ======================================================================
//  Entrainements
// ======================================================================

void Application::afficherEntrainements()
{
    titre("ENTRAINEMENTS");
    const vector<Entrainement> entrainements = m_entrainements.tous();
    if (entrainements.empty()) {
        cout << "Aucun entrainement pour le moment.\n";
    }
    for (const Entrainement& e : entrainements) {
        const StatistiquesPresence stats = m_entrainements.statistiques(e.getId());
        cout << "[" << e.getId() << "] " << e.getDateHeure() << " - " << e.getLieu()
             << " - " << nomEquipe(e.getEquipeId())
             << " - " << stats.inscrits << " inscrit(s)"
             << (m_entrainements.estInscrit(e.getId(), m_connecte->getId()) ? " (vous etes inscrit)" : "") << '\n';
        if (!e.getDescription().empty()) {
            cout << "     " << e.getDescription() << '\n';
        }
    }
}

void Application::creerEntrainement()
{
    titre("CREER UN ENTRAINEMENT");
    const string dateHeure = lireTexte("Date et heure (AAAA-MM-JJ HH:MM)");
    const string lieu = lireTexte("Lieu");
    const optional<int> equipe = choisirEquipe(true);
    const string description = lireTexte("Description (facultatif)");

    const Entrainement e = m_entrainements.creer(*m_connecte, dateHeure, lieu, equipe, description);
    cout << "Entrainement cree (n" << e.getId() << "), "
         << m_entrainements.statistiques(e.getId()).inscrits << " joueur(s) inscrit(s) automatiquement.\n";
}

void Application::modifierEntrainement()
{
    afficherEntrainements();
    const optional<int> id = lireEntier("Numero de l'entrainement");
    if (!id) {
        return;
    }
    const optional<Entrainement> e = m_entrainements.parId(*id);
    if (!e) {
        throw DonneeInvalide("Cet entrainement n'existe pas.");
    }
    const string dateHeure = lireTexte("Date et heure (AAAA-MM-JJ HH:MM)", e->getDateHeure());
    const string lieu = lireTexte("Lieu", e->getLieu());
    const optional<int> equipe = choisirEquipe(true, e->getEquipeId());
    const string description = lireTexte("Description", e->getDescription());
    m_entrainements.modifier(*m_connecte, *id, dateHeure, lieu, equipe, description);
    cout << "Entrainement modifie.\n";
}

void Application::supprimerEntrainement()
{
    afficherEntrainements();
    const optional<int> id = lireEntier("Numero de l'entrainement a supprimer");
    if (!id) {
        return;
    }
    if (confirmer("Supprimer cet entrainement et ses presences ?")) {
        m_entrainements.supprimer(*m_connecte, *id);
        cout << "Entrainement supprime.\n";
    }
}

void Application::sInscrireEntrainement()
{
    afficherEntrainements();
    const optional<int> id = lireEntier("Numero de l'entrainement");
    if (!id) {
        return;
    }
    m_entrainements.inscrire(*id, m_connecte->getId());
    cout << "Vous etes inscrit.\n";
}

void Application::seDesinscrireEntrainement()
{
    afficherEntrainements();
    const optional<int> id = lireEntier("Numero de l'entrainement");
    if (!id) {
        return;
    }
    m_entrainements.desinscrire(*id, m_connecte->getId());
    cout << "Vous etes desinscrit.\n";
}

// Le responsable scanne le QR code de chaque joueur. Une application de scan
// (ou une douchette USB) "tape" le contenu du QR code : il suffit de le coller ici.
void Application::scannerPresences()
{
    afficherEntrainements();
    const optional<int> id = lireEntier("Numero de l'entrainement");
    if (!id) {
        return;
    }
    if (!m_entrainements.parId(*id)) {
        throw DonneeInvalide("Cet entrainement n'existe pas.");
    }

    titre("SCAN DES PRESENCES");
    cout << "Scannez (ou collez) le contenu de chaque QR code. Ligne vide pour terminer.\n";
    while (true) {
        const string contenu = lireTexte("QR code");
        if (contenu.empty()) {
            break;
        }

        optional<Utilisateur> joueur;
        const ResultatScan resultat = m_entrainements.validerPresence(*m_connecte, *id, contenu, joueur);
        switch (resultat) {
        case ResultatScan::PresenceValidee:
            cout << "  OK : " << joueur->nomComplet() << " est present.\n";
            break;
        case ResultatScan::AjouteEtValide:
            cout << "  OK : " << joueur->nomComplet() << " n'etait pas inscrit, il a ete ajoute et marque present.\n";
            break;
        case ResultatScan::DejaPresent:
            cout << "  Deja scanne : " << joueur->nomComplet() << ".\n";
            break;
        case ResultatScan::QrCodeInconnu:
            cout << "  QR code inconnu.\n";
            break;
        }

        const StatistiquesPresence stats = m_entrainements.statistiques(*id);
        cout << "  -> " << stats.presents << " present(s) sur " << stats.inscrits << " inscrit(s)\n";
    }
}

void Application::voirPresences()
{
    afficherEntrainements();
    const optional<int> id = lireEntier("Numero de l'entrainement");
    if (!id) {
        return;
    }

    const vector<int> presents = m_entrainements.presents(*id);
    const vector<int> inscrits = m_entrainements.inscrits(*id);
    const StatistiquesPresence stats = m_entrainements.statistiques(*id);

    titre("PRESENCES");
    ostringstream taux;
    taux.precision(0);
    taux << fixed << stats.taux();
    cout << stats.presents << " present(s) sur " << stats.inscrits << " inscrit(s) (" << taux.str() << " %)\n";

    for (int joueurId : inscrits) {
        const bool present = find(presents.begin(), presents.end(), joueurId) != presents.end();
        cout << "  [" << (present ? "X" : " ") << "] " << nomJoueur(joueurId) << '\n';
    }
}

// ======================================================================
//  Matchs
// ======================================================================

void Application::afficherMatchs()
{
    titre("MATCHS");
    const vector<Match> matchs = m_matchs.tous();
    if (matchs.empty()) {
        cout << "Aucun match pour le moment.\n";
    }
    for (const Match& m : matchs) {
        cout << "[" << m.getId() << "] " << m.getDateHeure() << " - " << competitionVersTexte(m.getCompetition())
             << " - " << nomEquipe(m.getEquipeId()) << " contre " << m.getAdversaire()
             << " - " << m.getLieu() << " - " << m.scoreTexte() << '\n';
        if (!m.getDescription().empty()) {
            cout << "     " << m.getDescription() << '\n';
        }
    }
}

void Application::creerMatch()
{
    titre("CREER UN MATCH");
    const optional<int> equipe = choisirEquipe(false);
    if (!equipe) {
        return;
    }
    const string adversaire = lireTexte("Nom de l'adversaire");
    const TypeCompetition competition = choisirCompetition();
    const string dateHeure = lireTexte("Date et heure (AAAA-MM-JJ HH:MM)");
    const string lieu = lireTexte("Lieu");
    const string description = lireTexte("Description (facultatif)");

    const Match m = m_matchs.creer(*m_connecte, dateHeure, lieu, *equipe, adversaire, competition, description);
    cout << "Match cree (n" << m.getId() << ").\n";
}

void Application::modifierMatch()
{
    afficherMatchs();
    const optional<int> id = lireEntier("Numero du match");
    if (!id) {
        return;
    }
    const optional<Match> m = m_matchs.parId(*id);
    if (!m) {
        throw DonneeInvalide("Ce match n'existe pas.");
    }
    const optional<int> equipe = choisirEquipe(false, m->getEquipeId());
    if (!equipe) {
        return;
    }
    const string adversaire = lireTexte("Nom de l'adversaire", m->getAdversaire());
    const TypeCompetition competition = choisirCompetition(m->getCompetition());
    const string dateHeure = lireTexte("Date et heure (AAAA-MM-JJ HH:MM)", m->getDateHeure());
    const string lieu = lireTexte("Lieu", m->getLieu());
    const string description = lireTexte("Description", m->getDescription());
    m_matchs.modifier(*m_connecte, *id, dateHeure, lieu, *equipe, adversaire, competition, description);
    cout << "Match modifie.\n";
}

void Application::saisirScore()
{
    afficherMatchs();
    const optional<int> id = lireEntier("Numero du match");
    if (!id) {
        return;
    }
    const optional<Match> m = m_matchs.parId(*id);
    if (!m) {
        throw DonneeInvalide("Ce match n'existe pas.");
    }

    cout << "Saisissez le score de chaque set sous la forme \"25-20\" (" << nomEquipe(m->getEquipeId())
         << " d'abord, puis " << m->getAdversaire() << ").\nLigne vide quand tous les sets sont saisis.\n";
    vector<ScoreSet> sets;
    while (sets.size() < 5) {
        const string ligne = lireTexte("Set " + to_string(sets.size() + 1));
        if (ligne.empty()) {
            break;
        }
        ScoreSet set;
        char tiret = 0;
        istringstream lecture(ligne);
        if (!(lecture >> set.pointsEquipe >> tiret >> set.pointsAdversaire) || tiret != '-') {
            cout << "Format invalide, exemple : 25-20\n";
            continue;
        }
        sets.push_back(set);
    }

    m_matchs.enregistrerScore(*m_connecte, *id, sets);
    const optional<Match> misAJour = m_matchs.parId(*id);
    cout << "Score enregistre : " << (misAJour ? misAJour->scoreTexte() : "") << '\n';
}

void Application::supprimerMatch()
{
    afficherMatchs();
    const optional<int> id = lireEntier("Numero du match a supprimer");
    if (!id) {
        return;
    }
    if (confirmer("Supprimer ce match ?")) {
        m_matchs.supprimer(*m_connecte, *id);
        cout << "Match supprime.\n";
    }
}

// ======================================================================
//  Aides a la saisie
// ======================================================================

string Application::lireTexte(const string& question)
{
    cout << question << " : " << flush;
    string ligne;
    if (!getline(cin, ligne)) {
        throw FinDeSaisie{};
    }
    // Retire les espaces et le '\r' (Windows) en debut et fin de ligne.
    const size_t debut = ligne.find_first_not_of(" \t\r");
    if (debut == string::npos) {
        return "";
    }
    const size_t fin = ligne.find_last_not_of(" \t\r");
    return ligne.substr(debut, fin - debut + 1);
}

// Variante pour les modifications : une ligne vide garde la valeur actuelle.
string Application::lireTexte(const string& question, const string& valeurActuelle)
{
    const string saisie = lireTexte(question + " [" + valeurActuelle + "]");
    return saisie.empty() ? valeurActuelle : saisie;
}

optional<int> Application::lireEntier(const string& question)
{
    const string saisie = lireTexte(question);
    try {
        size_t lus = 0;
        const int valeur = stoi(saisie, &lus);
        if (lus == saisie.size()) {
            return valeur;
        }
    } catch (const exception&) {
    }
    if (!saisie.empty()) {
        cout << "Nombre attendu.\n";
    }
    return nullopt;
}

bool Application::confirmer(const string& question)
{
    const string reponse = lireTexte(question + " (o/n)");
    return reponse == "o" || reponse == "O" || reponse == "oui";
}

// Affiche la liste des membres et laisse l'admin choisir les joueurs de l'equipe.
vector<int> Application::choisirJoueurs(const vector<int>& actuels)
{
    cout << "\nJoueurs disponibles (X = deja dans l'equipe) :\n";
    for (const Utilisateur& u : m_utilisateurs.tous()) {
        const bool dedans = find(actuels.begin(), actuels.end(), u.getId()) != actuels.end();
        cout << "  [" << (dedans ? "X" : " ") << "] " << u.getId() << ". " << u.nomComplet()
             << " (" << u.getFiliere() << ")\n";
    }

    const string saisie = lireTexte(actuels.empty()
        ? "Numeros des joueurs separes par des espaces (ex : 1 4 7)"
        : "Numeros des joueurs separes par des espaces (ligne vide = garder la liste actuelle)");
    if (saisie.empty()) {
        return actuels;
    }

    vector<int> joueurs;
    istringstream lecture(saisie);
    string morceau;
    while (lecture >> morceau) {
        try {
            joueurs.push_back(stoi(morceau));
        } catch (const exception&) {
            cout << "\"" << morceau << "\" ignore (pas un numero).\n";
        }
    }
    return joueurs;
}

optional<int> Application::choisirEquipe(bool facultatif, optional<int> actuelle)
{
    const vector<Equipe> equipes = m_equipes.toutes();
    cout << "\nEquipes :\n";
    for (const Equipe& equipe : equipes) {
        cout << "  " << equipe.getId() << ". " << equipe.getNom() << '\n';
    }
    if (equipes.empty()) {
        cout << "  (aucune equipe)\n";
    }

    string question = "Numero de l'equipe";
    if (actuelle) {
        question += " [" + to_string(*actuelle) + "]";
    }
    if (facultatif) {
        question += " (0 = aucune)";
    }

    const string saisie = lireTexte(question);
    if (saisie.empty()) {
        return actuelle; // garder l'equipe actuelle (ou rien)
    }
    try {
        const int id = stoi(saisie);
        if (id == 0 && facultatif) {
            return nullopt;
        }
        for (const Equipe& equipe : equipes) {
            if (equipe.getId() == id) {
                return id;
            }
        }
    } catch (const exception&) {
    }
    throw DonneeInvalide("Equipe inconnue.");
}

TypeCompetition Application::choisirCompetition(optional<TypeCompetition> actuelle)
{
    while (true) {
        string question = "Type de competition : 1 = CFE, 2 = ACAD";
        if (actuelle) {
            question += " [" + competitionVersTexte(*actuelle) + "]";
        }
        const string saisie = lireTexte(question);
        if (saisie.empty() && actuelle) {
            return *actuelle;
        }
        if (saisie == "1" || saisie == "CFE" || saisie == "cfe") {
            return TypeCompetition::CFE;
        }
        if (saisie == "2" || saisie == "ACAD" || saisie == "acad") {
            return TypeCompetition::ACAD;
        }
        cout << "Repondez 1 ou 2.\n";
    }
}

string Application::nomJoueur(int utilisateurId)
{
    const optional<Utilisateur> u = m_utilisateurs.parId(utilisateurId);
    return u ? u->nomComplet() : "membre #" + to_string(utilisateurId);
}

string Application::nomEquipe(optional<int> equipeId)
{
    if (!equipeId) {
        return "toutes equipes";
    }
    const optional<Equipe> equipe = m_equipes.parId(*equipeId);
    return equipe ? equipe->getNom() : "equipe #" + to_string(*equipeId);
}
