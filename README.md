# AS Volley - INSA Strasbourg

Programme console (C++17, POO) de gestion du club : membres, equipes, entrainements
avec validation des presences par QR code, et matchs (CFE / ACAD) avec le detail du score.

## Lancer le programme

1. Definir l'adresse de la base PostgreSQL (Neon) dans la variable `DATABASE_URL`
   (elle n'est **jamais** ecrite dans le code, le depot est public) :
   - PowerShell : `$env:DATABASE_URL="postgresql://utilisateur:motdepasse@hote/base?sslmode=require"`
   - bash : `export DATABASE_URL="postgresql://utilisateur:motdepasse@hote/base?sslmode=require"`
2. Compiler avec CMake (les dependances `libpqxx` et `nayuki-qr-code-generator` sont installees par vcpkg).
3. Lancer `app`. Les tables sont creees automatiquement au demarrage.

## Comptes des joueurs

1. **Installation** : tant que le club n'a aucun admin, l'accueil propose *Creer le compte administrateur du club*.
2. **Import de la liste** : l'admin enregistre le tableau Excel des joueurs en CSV
   (*Fichier > Enregistrer sous > CSV UTF-8* ou *CSV (separateur : point-virgule)*) puis l'importe via
   *Membres du club > Importer la liste des joueurs*. La premiere ligne doit contenir les titres des
   colonnes : au moins `Nom` et `Licence` (`Prenom` et `Filiere` sont aussi reconnus, les autres colonnes
   sont ignorees). On peut reimporter la liste mise a jour a tout moment : les comptes existants sont gardes.
3. **Premiere connexion** : le joueur saisit son numero de licence et choisit son mot de passe.
4. **Connexions suivantes** : numero de licence + mot de passe, puis inscription aux seances.
5. **Mot de passe oublie** : un admin le reinitialise (*Membres du club*), le joueur en choisit un nouveau.

## Organisation du code

| Classe | Role |
|---|---|
| `Utilisateur` / `UtilisateurManager` | membres : nom, prenom, filiere, numero de licence (identifiant), statut client/admin, QR code personnel, activation du compte |
| `ListeJoueurs` | lecture du fichier CSV exporte d'Excel (liste des joueurs) |
| `Equipe` / `EquipeManager` | equipes : nom, photo (stockee dans la base), liste des joueurs |
| `Entrainement` / `EntrainementManager` | seances : date, lieu, inscriptions, presences validees par QR code |
| `Match` / `MatchManager` | matchs : equipe, adversaire, CFE/ACAD, score set par set |
| `Manager` | classe mere des managers : connexion a la base + verification des droits admin |
| `QrCode` | dessine un QR code dans la console ou dans un fichier SVG |
| `Application` | menus de la console |
| `Init` | creation des tables |

Les regles (droits admin, verifications) sont dans les classes `*Manager`, pas dans les menus :
le futur site web pourra les reutiliser telles quelles.

## Presences par QR code

- Chaque membre a un QR code personnel (menu *Mon profil et mon QR code*), a afficher ou enregistrer en SVG.
- Au debut de l'entrainement, le responsable (admin) ouvre *Entrainements > Scanner les QR codes*
  et scanne le QR code de chaque joueur. Le programme affiche `presents / inscrits`.
- En console, le contenu du QR code est saisi au clavier : une appli de scan sur telephone
  ou une douchette USB peut le "taper". La lecture directe par la camera du telephone
  sera ajoutee avec le site web.
