# Téléchargements du navigateur Vex

Les téléchargements utilisent le vrai chemin WebKit/WPE : le NetworkProcess
transfère le corps de la réponse par blocs vers un fichier VexFS ordinaire. Aucun
chemin de stockage propre au navigateur, aucune pile HTTP supplémentaire, et
rien n'est jamais exécuté ni inspecté.

## Chemin

```
page WebKit ── réponse (pièce jointe / type non affichable) ──► decide-policy
   webkit_downloads.c : webkit_policy_decision_download()
        │  signal « download-started » de WebKitNetworkSession
        ▼
   gestionnaire (webkit_linux_main) : un état par téléchargement
        │  VEX_BROWSER_DOWNLOAD_REPORT  (canal de contrôle existant)
        ▼
   superviseur natif /webkit ── VEX_VIEWPORT_DOWNLOAD ──► noyau (browser_downloads.c)
        ▲                                                    │
        └── « browser:download-cancel:<id>: » ◄── shell `downloads cancel <id>`
```

* Destination : `/home/root/Downloads`, créé à la demande par `mkdir`.
* Nom : proposé par le serveur (`Content-Disposition`) sinon dernier segment de
  l'URI ; réduit à un composant sûr, borné à 64 octets en conservant l'extension
  (`vex_download_names.h`). Collision : `nom (1).ext`, `nom (2).ext`… Deux
  téléchargements parallèles du même nom se réservent leur nom mutuellement.
* Publication : WebKit écrit dans `<nom>.vexpart.wkdownload`, puis sur
  `<nom>.vexpart` ; le fichier n'est renommé sous son nom final qu'après succès.
  Le nom final est d'abord réservé par un `open(O_CREAT|O_EXCL)`. Un échec ou une
  annulation supprime les fichiers partiels ; ceux qu'un crash aurait laissés sont
  supprimés au démarrage suivant (`*.vexpart`, `*.vexpart.wkdownload`).
* États : `running`, `finished`, `failed` (avec message), `cancelled`, plus octets
  reçus / total (inconnu = 0). Une troncature (connexion fermée avant le
  `Content-Length` annoncé, hors contenu encodé) est un échec.
* Une navigation devenue téléchargement n'est pas une erreur : le chrome réaffiche la
  page précédente, ou la page d'accueil native d'un onglet vide
  (`VEX_BROWSER_DIVERTED`).
* Fermeture du navigateur : tous les transferts sont annulés, WebKit acquitte
  (attente bornée 1,5 s), les partiels sont supprimés ; le noyau marque les
  téléchargements restants comme annulés.

## Commandes du shell Vex

| Commande | Effet |
|---|---|
| `downloads` | liste les téléchargements de la session |
| `downloads cancel <id>` | demande l'annulation d'un téléchargement en cours |
| `fdstats` | nombre de descripteurs ouverts par processus (diagnostic de fuite) |

## Changement noyau lié

`rename()` suit désormais POSIX : un fichier régulier existant est remplacé
atomiquement (`fs_move_replace`, une seule transaction de table). GIO (donc WebKit)
publie ses fichiers temporaires ainsi ; l'ancien comportement (`EEXIST`) faisait
échouer tout téléchargement avec « Error moving file … File exists ».

## Tests

* Hôte : `userspace/linux_tests/download_names_test.c` (noms, URI, collisions,
  budget de chemin) et `scripts/download-verify-selftest.py` (cohérence du serveur de
  test et du vérificateur).
* VM : `scripts/download-test-server.py` (hôte ; l'invité le joint via
  `http://10.0.2.2:8765/`) et `/usr/linux-tests/download_verify` (invité : relit
  chaque fichier et compare octet par octet au contenu attendu).
  Scénarios : texte, binaire de 8 Mo, deux transferts simultanés de même nom, taille
  inconnue (chunked), coupure en cours de transfert, annulation, fermeture du
  navigateur pendant un transfert, nom hostile (`../../etc/pwn`), clic réel sur un
  lien et `<a download>`, HTTPS réel (proof.ovh.net 1 Mo et 10 Mo).

## Limites connues

* Pas d'interface de téléchargements dans le chrome natif : progression et annulation
  passent par `downloads` ; les téléchargements ne s'affichent pas dans une fenêtre.
* VexFS garde un fichier ouvert entièrement en mémoire noyau jusqu'à sa fermeture et
  ne compte que 64 entrées au total : un téléchargement occupe deux entrées pendant
  son transfert, et un très gros fichier est limité par la RAM. Le code du navigateur
  n'ajoute aucun tampon (écriture par blocs par WebKit) ; lever la limite relève du FS.
* Les chemins sont limités à 128 octets (contrat VexFS).
* Le texte via HTTPS n'a pas pu être testé avec un serveur à certificat de confiance
  (HTTPS réel validé avec des binaires).
