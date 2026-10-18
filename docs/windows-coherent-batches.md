# Windows — lots cohérents, checkpoint du 12 octobre 2026

Le chantier reste ouvert. Le point d'entrée de Chrome **n'est pas atteint**.
Les PE Chrome de validation restent identiques aux fichiers installés du host.
Ni conversion PE→ELF, ni DLL Windows host exécutée comme solution de compatibilité.

## Inventaire et stratégie

`audit-pe-imports.ps1` utilise maintenant le graphe récursif `audit-pe-graph.py`,
et non un relevé objdump limité au premier exécutable. Le graphe final est filtré
par le census du resolver réellement compilé et exécuté en VM : 346 bindings
(162 kernel32, 162 kernelbase, 19 ntdll, 3 version). Les doublons entre DLL sont
comptés comme bindings ; ce chiffre n'est ni une conformité ni 346 API complètes.

Graphe : 364 modules, 100709 edges, 766 API sets, zéro erreur de parsing.
Les DLL host transitives et leurs API-set mappings sont **référence seulement**.
Les forwarders, imports retardés et sites de résolution dynamique sont séparés.
379 contrats statiques restent manquants dans les trois racines analysées,
qui incluent chrome.dll (non stagé). Pour chrome.exe + chrome_elf.dll seuls,
129 contrats uniques restent manquants dans l'inventaire. Les imports retardés
manquants sont 796. Le gros total de la fermeture System32 ne doit pas être
interprété comme le nombre de contrats nécessaires avant l'entrée de Chrome.

Le loader valide maintenant la table entière de chaque module avant de refuser
un graphe incomplet. Il trace les manques avec le nom de l'appelant (128 lignes
max par module), sans cible IAT de substitution ni exécution d'une image échouée.
GetProcAddress trace ses premiers échecs avec module, symbole/ordinal, call site
et erreur. Le harness distingue explicitement un refus de chargement attendu
et une exécution fonctionnelle ; les bindings absents rendent le test rouge.

## Contrats supplémentaires réellement implémentés

- CreateProcessA rejoint CreateProcessW avec l'ACP UTF-8 réellement configurée.
  Commande, environnement A/W, pipes hérités, création suspendue, reprise,
  attente, exit code et indépendance des handles sont exercés en PE réel.
  Une HANDLE_LIST explicite ne peut plus être élargie par les handles stdio.
  Si STARTF_USESTDHANDLES réclame un handle hors de cette sélection, cette
  combinaison est explicitement refusée (50), sans lancer d'enfant.
  Une liste d'attributs sans HANDLE_LIST garde l'héritage ordinaire.
- GetThreadId utilise les métadonnées réelles du contexte/local ou du thread
  primaire. OpenProcess dérive une capability de self ou d'un processus déjà
  possédé ; un PID seul n'est jamais une autorité. Query, wait et terminate sont
  distincts et atténués. VM-memory/remote injection/autres droits restent refusés.
  WAIT_POLL est une consultation native atomique sous droit WAIT, sans sleep
  ni timeout artificiel ; la compatibilité ne requiert plus QUERY pour attendre.
- GetProcessHeaps décrit l'unique heap Windows actuellement fourni.
- Métadonnées confinées GetFileAttributes W/A/ExW, GetFileSize, GetFileTime,
  CreateFileA ; dates réelles accessibles par STAT_EX. La date de création
  non exposée par cette ABI est zéro, jamais une substitution par ctime.
- DeleteFile W/A et RemoveDirectory W/A exécutent une vraie suppression via
  root capability. La transaction protège résolution, droits et publication
  des opens. Les objets fichier vivants restent inventoriés même sans FD
  (mapping ou attachment). Leur suppression est refusée EBUSY→50 jusqu'au
  vrai delete-pending Windows ; elle ne peut être annoncée comme réussie puis
  recréer un nom au dernier flush. Liens terminaux refusés explicitement.
- Condition variables : SRW exclusive/shared et critical-section, capture de
  génération avant unlock, compare-and-sleep futex, WakeOne/WakeAll, timeout,
  réacquisition du verrou. Aucun polling ni waiter userspace conservé en kernel.
- InitOnce : exécution unique sync, contenders bloqués, publication acquire/
  release du contexte, rollback d'échec, CHECK_ONLY et tentatives async avec
  completion CAS ; modes incompatibles et contexte non aligné refusés.
- VERSION.dll : vraie DLL PE adapter, RT_VERSION lu dans le fichier PE confiné,
  extraction immutable, valeurs fixes, translation et chaînes UTF-16 via
  VerQueryValueW. Aucun numéro de version inventé. Couverture actuelle AMD64/
  ressource de base ; fusion MUI, version A/Ex et search complet restent ouverts.
- NTDLL : NtClose ferme le vrai handle typé et renvoie NTSTATUS en conservant
  LastError Win32 ; RtlInitUnicodeString/Ex calculent les longueurs en octets,
  RtlFreeUnicodeString libère les buffers réellement alloués sur le heap.

Limites CreateProcess restantes : tokens/SD arbitraires, jobs, généralisation des
flags et attributs, recherche exe complète, env Unicode >32768 unités, inheritance
événements/sections/named-pipes, fichiers associés à IOCP et DuplicateHandle
étranger. Les refus sont explicites. CreateProcess n'est **pas complet pour tous
les flags Windows**, et ces limites ne sont pas dissimulées derrière un PID réussi.

## Validation réelle

- `windows-version-process-1-cpu-20261012-044604.log` et
  `windows-version-process-4-cpu-20261012-044835.log` : runner complet PASS.
- `windows-nt-core-4-cpu-20261012-045441.log` : nouveau test NT + runner complet
  + census + lancement Chrome réel. Le test NT + census + lancement Chrome passe aussi dans
  `windows-nt-core-1-cpu-20261012-045746.log`.
- 400 handoffs condition variables, huit shared readers, CS timeout/reacquire,
  huit contenders InitOnce, retry et completion async : PASS 1/4 CPU.
- Ressource VERSIONINFO produite par llvm-rc, contenant vraies valeurs binaires,
  translation et strings, erreurs/malformed/confinement : PASS 1/4 CPU.
- Processus PE parent/enfant : quatre cycles dont A et W, input/output réels et
  EOF, héritage sélectif, suspend/resume, query-only vs wait-only vs terminate,
  PID/TID, refus/rollback, terminate : PASS 1/4 CPU.
- Linux TLS/FPU, W^X, faults, memory ownership, native console, root/IPC,
  CFG/VEH, ORA/lifetime, IOCP et libc pthread (187 checks, zéro échec) : PASS.
- Native root removal : permissions, escapes, live writer et référence conservée
  uniquement par mapping, suppression après dernière référence : PASS 4 CPU.
- Parser/graphe : cinq tests PASS. Diff whitespace check PASS.
- Références Windows host légères : InitOnce CHECK_ONLY et completion invalide
  errno=31 ; version short buffer errno=122 ; RtlInitUnicodeString long
  length/max=65532/65534. CreateProcess host unsigned reste refusé 577 : pas
  de contournement de cette politique de sécurité.

Un premier runner 4 CPU a signalé native-execution FAIL sans numéro de ligne.
Le test a été instrumenté ; il passe ensuite isolément et dans les runners
complets 1/4 CPU. Cause initiale non établie : ne pas présenter ce point comme
un bug corrigé ni effacer la trace `windows-path-batch-4-cpu-20261011-234535.log`.

## Frontière Chrome actuelle

Chrome EXE → chrome_elf.dll → table statique validée → **94 contrats manquants**
dans chrome_elf.dll → refus de chargement explicite. VERSION.dll n'est plus absent.
Le même résultat est constaté en 1/4 CPU. Pas d'entrée EXE, de fenêtre ou navigation
Chrome annoncée. Familles importantes restantes : NT registry dans le préfixe,
SEH/unwind x64 réel, job/supervision, mémoire/query, system/NLS, file enumeration
et I/O/attentes multiples. API sets runtime et unload/TLS dynamiques restent partiels.

## Image dédiée

`build/kyilos-windows-process.iso` — aucune image Dell/USB ni VM humaine remplacée.

```
ISO      FDC2A42C7F8B69B408DB11392C63644346ED9FCF88ABB6ECD7FE77A0D6F0D5B6
kernel   4C07613F995D97002BC69A0EE0AB5E17B2365F26F1570631F3AFA4BE46B25874
runtime  C43C7266B452300A630844563FD832D940F2C281213B5E1818DA9E38546FBA42
Chrome   D3784FFBF1F6109348416064B3E4CD739B06FA61D89A81B262C780DF9F32270C
elf.dll  C0DDDE7674083BA53D40CE1913C7F505C94157484D7BE8F9EFA060EED307210A
```

Références de contrat :
[condition variables](https://learn.microsoft.com/en-us/windows/win32/sync/condition-variables),
[InitOnce](https://learn.microsoft.com/en-us/windows/win32/sync/one-time-initialization),
[version resources](https://learn.microsoft.com/en-us/windows/win32/api/winver/nf-winver-verqueryvaluew),
[DeleteFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew).

