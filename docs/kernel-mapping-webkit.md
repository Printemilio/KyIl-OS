# Publication des mappings noyau — mesure WebKit, 4 octobre 2026

## Contrat vérifié et correction

`vmm_new_address_space()` copie les entrées PML4 256–511, donc leurs
PDPT/PD/PT restent physiquement partagés. La moitié utilisateur reste propre
à l'espace. `process_group_attach()` partage le CR3 entre threads ; le spawn
temporaire peut également emprunter un CR3 entre groupes différents.

Les seuls appelants de `scheduler_map_kernel_page_all()` étaient
`new_chunk()` et `new_dedicated_chunk()` dans MRAC. Ils installaient déjà le
mapping dans la racine noyau avant de parcourir toutes les tâches. Le chemin
de libération `release_chunk_backing()` faisait le travail symétrique, puis
un second unmap noyau. Chaque visite réécrivait la même PTE partagée et
recopiait 256 entrées PML4, même pour plusieurs threads ayant le même CR3.

Le VMM conserve désormais un registre privé des racines créées par sa
factory, indépendamment des tâches et de l'ownership des groupes. Un mapping
noyau modifie une seule fois l'arbre canonique partagé. Seule l'apparition
d'une nouvelle entrée PML4 publie cette entrée vers chaque racine vivante.
Les racines en cours de chargement sont incluses ; une racine créée ensuite
hérite des branches sous le même verrou. La destruction retire la racine
du registre avant de libérer ses tables. Les tables inférieures noyau ne
sont toujours pas détruites par le teardown utilisateur.

Les deux helpers scheduler restent des délégations pour leurs appelants
internes ; ils ne parcourent plus les tâches. MRAC n'effectue plus le
map/unmap canonique supplémentaire. Les autres appels VMM de mappings
noyau, notamment MMIO, suivent aussi le propriétaire canonique.

Le registre est protégé par un verrou acquire/release avec sauvegarde des
interruptions locales. Ses métadonnées utilisent une page PMM par racine,
libérée avec celle-ci, sans allocation récursive par MRAC/heap et sans limite
fixe du nombre de racines. Les droits et le filtrage USER sont inchangés.
L'invalidation locale INVLPG est conservée. Le dépôt n'implémente pas de
démarrage des AP ni de shootdown TLB inter-CPU : cette passe ne constitue
pas une validation SMP, et n'ajoute pas une architecture SMP parallèle.

## Navigation réelle avant/après

Même ISO userspace/WebKit/JSC, même recherche native `pomme fruit`, même
VirtualBox 1 vCPU / 4096 MiB, un boot neuf pour chaque mesure. Seul le kernel
et les compteurs de cette passe changent. Pas de modification réseau, JIT,
shader ou FS_BASE. Les documents DuckDuckGo ne sont pas des fixtures : leurs
octets et sous-ressources varient. Ces deux runs ne donnent pas de variance
statistique ni de gain garanti.

| Mesure | Avant instrumenté | Après |
|---|---:|---:|
| TTFB | 171 ms | 160 ms |
| Transfert document | 2589 ms | 1870 ms |
| DOMInteractive | 53300 ms | 38679 ms |
| DOMContentLoaded | 67460 ms | 51009 ms |
| First paint déclaré WebKit | 67500 ms | 51049 ms |
| DOMComplete | 151300 ms | 123439 ms |
| START → LOAD_FINISHED | 171850 ms | 146160 ms |

La précédente navigation non instrumentée avait DOMInteractive=42799 ms et
LOAD_FINISHED=175430 ms. Le DOM reste donc lent (~39 s) : on ne peut attribuer
toute sa durée à ce mécanisme. First paint est la métrique WebKit, pas une
mesure indépendante du moment de présentation écran. Les deux pages ont
été inspectées visuellement ; les captures sont dans `build/kmap-*-result.png`.

Les compteurs sont remis à zéro dans la page native d'accueil, puis relevés
après chargement. Les fenêtres de relevé ne sont pas strictement identiques
et incluent du travail après LOAD_FINISHED : comparer le travail par appel
et la redondance, pas les totaux comme des débits.

| Compteur | Avant | Après |
|---|---:|---:|
| Appels map MRAC | 937266 | 1862371 |
| Appels unmap MRAC | 930167 | 1856692 |
| Visites de tâches (map) | 44014119 | 0 |
| Espaces distincts cumulés (map) | 4685498 | 0 (aucun parcours nécessaire) |
| Visites de CR3 déjà rencontré (map) | 39328621 | 0 |
| Appels de synchronisation PML4 | 76531582 | 45 |
| Entrées PML4 examinées | 19592084992 | 11520 |
| Écritures d'entrées PML4 | 19592084992 | 0 |
| Changements d'entrées PML4 | 0 | 0 |
| Écritures de feuilles noyau | 44951385 | 1862371 |
| Publications d'une nouvelle branche pendant la navigation | non instrumentées séparément | 0 |
| Racines vivantes au relevé | non enregistrées | 5 |

89,35 % des visites map avant correction répétaient un CR3. Les 45 scans
restants après correction sont le chemin existant de création de tâches,
pas un coût par page MRAC. Le nombre de feuilles écrites correspond désormais
au nombre d'appels map, au lieu d'environ 48 écritures par appel avant.

## Profil comparable

160 prélèvements RIP par run, immédiatement après validation de la recherche,
via VBoxManage, espacements variables identiques, symbolisation contre le
kernel exact de chaque run. Échantillons indicatifs, pas des pourcentages CPU
exacts ni une trace complète de la navigation.

| Fonction | Avant /160 | Après /160 |
|---|---:|---:|
| vmm_sync_kernel_mappings | 25 | 0 |
| scheduler_map_kernel_page_all | 5 | 0 |
| scheduler_unmap_kernel_page_all | 2 | 0 |
| vmm_map_page | 2 | 0 |
| vmm_unmap_page | 3 | 0 |
| invalidate_page | 4 | 0 |
| vmm_unmap_kernel_page | 0 | 1 |
| zero_bytes (MRAC) | 22 | 29 |
| MRAC_hash_insert | 12 | 16 |
| find_entry | 10 | 10 |
| memset | 6 | 9 |
| Userspace non symbolisé | 15 | 27 |

Les fonctions de mapping passent de 41 à 1 échantillon ; MRAC/initialisation
mémoire restent présents. Aucun autre sous-système n'a été optimisé.

## Tests ciblés et preuves

`kmaptest` est un test explicite, jamais exécuté au boot normal. Il crée
six vrais contextes scheduler, trois sur chacune de deux racines distinctes,
puis installe un mapping dans une nouvelle branche noyau. Il vérifie :

- une seule écriture de feuille et une publication par racine, indépendamment
  du nombre de threads ; lecture réelle des mêmes octets par les six contextes ;
- héritage par une troisième racine créée après publication ;
- NX, absence de USER sur la feuille et la racine, refus d'un mapping USER noyau ;
- visibilité de l'unmap dans les deux espaces, maintien du backing quand une
  racine disparaît et que les autres restent vivantes ;
- 32 créations/destructions de racines, retour exact du compteur PMM et du
  nombre de racines enregistrées.

Résultat VM final : PASS. Les smokes existants
`linux_thread_context_smoke`, `linux_memory_ownership_smoke` et
`linux_exec_args_smoke` atteignent `done` (vérifiés dans le terminal ; pas
toutes leurs lignes stdout apparaissent en série). Aucun panic constaté.
Pas de campagne globale de régressions.

Commandes natives : `kmapstats`, `kmapstats reset`, `kmaptest`.
Preuves : `build/kmap-before-navigation.log`, `build/kmap-after-navigation.log`,
`build/kmap-before-symbols.json`, `build/kmap-after-symbols.json`,
`build/kmap-targeted-regressions.log`, `build/kmap-exec-regression.png`.
Le dernier rebuild n'a modifié que le snapshot des compteurs et renforcé le
test de racine tardive ; le chemin de mapping mesuré est identique.
