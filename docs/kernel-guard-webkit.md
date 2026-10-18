# Transactions mémoire : optimisation ciblée et comparaison WebKit

## Changement

`gdt_init()` détecte une fois l'identifiant CPU avec les mêmes feuilles CPUID
qu'auparavant, puis le conserve dans l'état kernel GS existant. Il est appelé
avant les initialisations qui peuvent utiliser des guards. Le nouveau champ
est ajouté après les scratchs syscall : leurs offsets restent identiques.
Chaque CPU doit installer son instance GS avant d'utiliser les guards ; ce
changement ne crée pas un identifiant global partagé entre CPU. Vex démarre
actuellement le BSP ; la VM mesurée possède un vCPU, sans validation SMP.

`kernel_guard_enter()` conserve IRQ masking, exclusion, ownership CPU/TID,
récursion et protection contre la préemption. Les CPUID à chaque entrée sont
remplacés par une lecture `%gs:40`. `kernel_guard_leave()` est inchangé.

`vex_user_string_is_valid()` valide une portion de page une fois avant de
chercher son NUL, au lieu de revalider chaque caractère. Il conserve le même
validateur de plage, l'inventaire, les contrôles PRESENT/USER, les faults lazy,
les limites utilisateur et la longueur maximale. Il ne touche pas la page
suivante si le NUL est déjà trouvé. Les opérations WRITE et W^X ne changent pas.
Aucune optimisation de l'allocateur, du réseau, de JSC ou du renderer.

## Validation VM

- `tests/kernel_guard_test.c` : identité GS comparée au CPUID réel, ownership
  CPU/TID, entrée récursive, dernière libération, restauration IF et entrée
  avec IF déjà désactivé. **PASS**, depuis une tâche noyau schedulée.
- `linux_user_validation_test` : **13 étapes PASS**, sortie 0. Chaîne sur deux
  pages lazy, readonly, NUL au dernier octet avec page suivante désallouée,
  chaîne traversant un trou, absence de NUL, pointeurs NULL/invalide/kernel,
  fin de domaine utilisateur, PROT_NONE et chaîne vide lazy suivie de PROT_NONE.
- `linux_native_domain_smoke`, `linux_memory_ownership_smoke`,
  `linux_thread_context_smoke`, `linux_jit_wx_smoke` : **sortie 0**.
- Kernel reconstruit, boot et vraie page WebKit affichée. Désassemblage des
  binaires normal et de diagnostic : GS cache, aucun CPUID dans le guard.

Le premier test du cas vide attendait ENOENT de `stat("")`. Le code existant
normalise ce chemin vers le cwd ; cette attente ne testait pas la mémoire et
était incorrecte pour le contrat actuel. Le cas final utilise native DEBUG_LOG
sur une chaîne vide lazy, avec la page suivante PROT_NONE. Aucun comportement
filesystem ni code d'erreur n'a été modifié pour réussir le test.

Le test userspace est intégré à `scripts/build-linux-tests.ps1` et peut être
lancé avec `run /usr/linux-tests/linux_user_validation_test`. Le test guard est
un test kernel à lier au runner ; le runner diagnostic utilisé et ses traces
sont conservés sous `build/guard-after/`.

## Une navigation réelle « pomme fruit »

Même launcher `/webkit`, même saisie native, même URL HTTPS DuckDuckGo et
même instrumentation Navigation Timing/Paint Timing que le diagnostic
précédent. La navigation comparative n'est lancée qu'après la fin des tests.

| Étape | Avant | Après | Variation |
|---|---:|---:|---:|
| TCP hors TLS | 39 ms | 0 ms exposé | connexion réutilisée/phase non exposée |
| TLS | 35 ms | 0 ms exposé | idem |
| DNS | 0 ms exposé | 0 ms exposé | pas une mesure à froid |
| TTFB | 76 ms | 85 ms | +9 ms |
| responseStart | 311 ms | 142 ms | |
| responseEnd | 1 311 ms | 564 ms | |
| transfert document | 1 000 ms | 422 ms | −578 ms |
| DOMInteractive | 8 626 ms | 5 014 ms | −41,9 % |
| DOMContentLoaded | 10 916 ms | 6 578 ms | −39,7 % |
| first-contentful-paint | 10 936 ms | 6 588 ms | −39,8 % |
| domComplete | 19 204 ms | 11 211 ms | −41,6 % |
| loadEventEnd | 19 259 ms | 11 231 ms | −41,7 % |
| callback UI LOAD_FINISHED, origine UI distincte | 22 648 ms | 12 021 ms | −46,9 % |

Les durées Navigation Timing partagent navigationStart ; le callback UI
utilise WEBKIT_LOAD_STARTED, donc il ne faut pas additionner ces valeurs.
Le délai responseEnd → DOMInteractive passe de **7 315 à 4 450 ms (−39,2 %)** ;
responseEnd → FCP passe de **9 625 à 6 024 ms (−37,4 %)**. Le gain local reste
donc substantiel même en retirant le transfert initial plus rapide.

C'est une paire de runs Internet, pas un benchmark réseau contrôlé : le site
et ses expériences changent (23 635 puis 24 640 octets décodés). Les chiffres
ne permettent pas d'attribuer exclusivement chaque milliseconde au correctif,
ni de séparer le gain du cache CPU de celui des validations par page.

## Mini-profil CPU

Même sampler externe VirtualBox, intervalle **50 ms**, fenêtres de **2 s**,
RIP de tête symbolisé sur le binaire exact. Avant : 400 observations, fenêtres
09–18 (~20 s actives). Après : 200 observations, fenêtres 01–05 (~10 s,
2,1–12,6 s après Enter), la navigation étant nettement plus courte. Le dernier
intervalle recouvre légèrement la completion UI. Les phases et durées ne sont
pas strictement identiques ; ce sont des parts statistiques, pas des temps CPU
absolus. CPUID étant intercepté en VM, le résultat dépend aussi de VirtualBox.

| Catégorie / hotspot | Avant | Après |
|---|---:|---:|
| kernel_guard_enter | **38 % (152/400)** | **0 % observé (0/200)** |
| mémoire/kernel totale | **68 %** | **46,5 %** |
| zero_bytes → new_dedicated_chunk → ka_raw_alloc | 16,25 % | **26,5 %** |
| memset | 3,75 % | **6 %** |
| ka_hash_insert | 3,25 % | **4 %** |
| logs série inb/outb/debug_log_putchar | 5,5 % | **14,5 %** |
| autres kernel/syscalls/IRQ | 12,5 % | 13,5 % |
| renderer software/shaders commun Vex | non séparé | **10,5 %** |
| JSC identifié + arène RX | 1,5 % | **5 %** |
| DOM/style/layout identifié | 0,75 % | **1,5 %** |
| réseau identifié | 1,75 % | **3 %** |

Le guard reste exécuté : zéro observation ne signifie pas zéro coût. Il n'y
a plus de CPUID dans son chemin. Les poids relatifs des autres fonctions
augmentent lorsqu'un gros coût disparaît ; cela ne prouve pas une régression
de leur coût absolu.

La copie de diagnostic capture les mappings exécutables du WebProcess après
la navigation, hors des fenêtres de profil. Cela permet notamment de résoudre
`vk_spv_run`, `vex_spv_run_quad` et le rasterizer dans `libvex_graphics.so.1`.
Il s'agit du renderer commun, pas d'une nouvelle traduction GL→Vulkan.
Cinq rejets de shaders sont encore visibles avant le relevé final, comme
auparavant ; ils ne sont pas corrigés dans cette passe.

Le nouveau premier hotspot est l'allocation/initialisation de pages dans
`new_dedicated_chunk`. Les logs série constituent également un coût visible.
Ces chemins restent inchangés : aucune deuxième optimisation n'est engagée.

Preuves : `build/navigation-diagnostic/report.md` et
`build/guard-after/{navigation.log,sample-01.txt…sample-05.txt,symbols.json,
categories.json,page-final.png}`. L'ISO final est restauré en mode desktop,
avec le **kernel optimisé normal**, sans runner ni launcher instrumentés.
