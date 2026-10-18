import json,pathlib,re
root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/paint-comparison'
r=json.loads((out/'results.json').read_text());meta=json.loads((out/'metadata.json').read_text())
lines=['| Mesure | 1 CPU C/LVGL | 1 CPU Suchu/Raylib | 4 CPU C/LVGL | 4 CPU Suchu/Raylib |','|---|---:|---:|---:|---:|']
def row(name,fn):lines.append('| '+name+' | '+' | '.join(fn(x) for x in r)+' |')
row('RAM repos, Mio',lambda x:f"{x['ram_idle_mib']:.2f}")
row('RAM pendant dessin, Mio',lambda x:f"{x['ram_drawing_mib']:.2f}")
row('Pic observé, Mio',lambda x:f"{x['sampled_peak_mib']:.2f}")
row('Seconde instance, incrément Mio',lambda x:f"{x['second_increment_mib']:.2f}")
row('Deux instances, total Mio',lambda x:f"{x['pair_total_mib']:.2f}")
row('CPU repos, borne haute % cœur',lambda x:f"{x['idle_cpu_pct']:.2f}")
row('CPU charge, % cœur',lambda x:f"{x['load_one_core_pct']:.1f}")
row('CPU actif cumulé des 64 renders, ms',lambda x:f"{x['render_cpu_ms']:.1f}")
row('Démarrage première présentation, ms',lambda x:f"{x['startup_ms']:.1f}")
row('Rendu p50 / p95 / p99, ms',lambda x:' / '.join(f"{x['render_ms'][p]:.2f}"for p in ('p50','p95','p99')))
row('Présentation p50 / p95 / p99, ms',lambda x:' / '.join(f"{x['present_ms'][p]:.2f}"for p in ('p50','p95','p99')))
row('Intervalle frame p50 / p95 / p99, ms',lambda x:' / '.join(f"{x['interval_ms'][p]:.2f}"for p in ('p50','p95','p99')))
row('Débit réel du replay, FPS',lambda x:f"{x['fps']:.2f}")
row('Clic visible, borne haute host ms',lambda x:f"{x['visible_upper_ms']:.1f}")
for n in (1,4):
    a,b=[x for x in r if x['cpu_count']==n]
    lines+=['',f"À {n} vCPU : Suchu utilise **{b['ram_idle_mib']/a['ram_idle_mib']:.2f}×** la RAM au repos, "
        f"**{b['sampled_peak_mib']/a['sampled_peak_mib']:.2f}×** au pic, "
        f"**{b['startup_ms']/a['startup_ms']:.2f}×** le temps de démarrage, "
        f"**{b['render_ms']['p50']/a['render_ms']['p50']:.2f}×** le rendu médian et "
        f"**{b['render_cpu_ms']/a['render_cpu_ms']:.2f}×** le CPU actif de rendu. "
        f"Le débit est inférieur de **{100*(1-b['fps']/a['fps']):.1f}%**."]
lines+=['','La présentation reste proche : la majeure partie de l’écart est située avant cet appel. '
        'Les 4 vCPU n’accélèrent pas substantiellement ces renderers essentiellement monothread. '
        'La borne de clic visible (~200 ms) est dominée par la méthode de capture ; '
        'elle ne démontre pas un avantage de réactivité entre les piles.']
sizes=['| Exécutable | Taille fichier | Octets PT_LOAD en fichier | Dépendances dynamiques |','|---|---:|---:|---|']
for name in ('lvgl','lvgl_profile','suchu'):
    e=meta['executables'][name];sizes.append(f"| {name} | {e['bytes']:,} | {e['load_file_bytes']:,} | aucune |")
c=meta['code'];app_c=c['userspace/apps/vex_paint.c'];app_s=c['userspace/suchu/examples/paint.suchu']+c['userspace/suchu/examples/paint_model.suchu']
sizes+=['',f"Code applicatif, hors blancs/commentaires : **{app_c} lignes C**, "
    f"**{app_s} lignes Suchu** (interface {c['userspace/suchu/examples/paint.suchu']}, "
    f"modèle {c['userspace/suchu/examples/paint_model.suchu']}). "
    'Ces comptes incluent le replay optionnel dans les deux sources. '
    f"Réduction applicative : {100*(1-app_s/app_c):.1f}%. "
    'Bridge de logique Paint : **0 ligne C/OCaml** dans la version Suchu.',
    '',f"Instrumentation commune séparée : **{c['userspace/libvex/src/vex_bench.c']} lignes C** de compteurs/export "
    f"et **{c['userspace/apps/paint_bench_runner.c']} lignes C** de lancement/observation. "
    'Les wrappers de mesure ajoutent 6 lignes CAML et 12 lignes OCaml, '
    'hors headers et points d’appel. Ils ne contiennent aucune logique de dessin.',
    '', 'Infrastructure graphique générale : 16 lignes OCaml pour les deux nouvelles primitives de dessin ; '
    'les adaptateurs existants (GUI, Raylib, LVGL, ABI) sont réutilisés. '
    'Les modifications de clock et attente sont localisées dans les fonctions listées ci-dessous. '
    'Le dépôt contient des changements antérieurs non liés : un diff global Git ne serait pas '
    'un compte honnête des lignes de cette expérience.',
    '', 'Les tailles PT_LOAD excluent notamment les symboles/debug non chargés. '
    'La taille de fichier ne doit donc pas être interprétée comme le RSS. '
    'Le binaire Suchu est environ 19,2× plus gros sur disque ; les archives spécifiques '
    'sont intégrées statiquement, pas nécessaires comme fichiers séparés au lancement.']
validation=['Tests réellement exécutés : trois répétitions par pile à 1 et 4 vCPU, deux '
    'instances indépendantes, checksums communs, clics PS/2 et détection de pixels visibles. '
    'Les tests de production vérifient les trois couleurs, les tailles petite/grande, '
    'la sélection de la taille moyenne, gomme, clear stationnaire avant relâchement, '
    'drag et resize compact avec conservation du document. Neuf assertions Suchu '
    'vérifient DDA positive/négative, bornes, cap 4096, clear, gomme et la trace commune.',
    '', 'Comparaison des documents réellement capturés, toolbar exclue : '
    f"**{meta['visual'][0]['exact_document_pct']:.3f}%** de pixels identiques et "
    f"**{100*meta['visual'][0]['black_iou']:.2f}%** d’IoU des zones noires, "
    'reproduits dans les six paires de captures. Les écarts de contour ne sont pas masqués ; '
    'LVGL et RLSW ont des règles de couverture/antialiasing différentes.',
    '', '**Conclusion : pour ce Paint et ces backends software, la pile Suchu/Raylib '
    'actuelle n’est pas un meilleur remplacement général de C/LVGL en mémoire ou '
    'performances.** La réduction du code applicatif est réelle, et l’écriture de '
    'dessin 2D en Suchu reste intéressante, mais elle s’accompagne ici de beaucoup '
    'plus de RAM, de CPU par travail identique et de frames lourdes moins régulières. '
    'Le batch/cache ne change pas cette conclusion. Une migration globale motivée '
    'par un gain de vitesse n’est pas étayée. Pour des applications scriptables, '
    'Suchu peut être choisi pour ses qualités de langage ; le prix runtime/rendu '
    'doit rester explicite. Ce résultat ne classe pas Raylib isolé contre LVGL isolé '
    'et ne prédit pas les performances d’un futur backend GPU.',
    '', 'Fichiers applicatifs : `userspace/apps/vex_paint.c`, '
    '`userspace/suchu/examples/paint.suchu`, `paint_model.suchu`, `paint_test.suchu`.',
    '', 'Adaptation/infrastructure : `kernel/src/scheduler.c` (clock CPU réelle), '
    '`kernel/src/vex_posix_compat.c` (clocks 2/3), `kernel/src/vex_syscall.c` '
    '(attente deadline), `kernel/include/unos/vex_abi.h` (flag timeout), '
    '`userspace/libvex/include/vex_app.h`, `userspace/libvex/src/vex_lvgl.c`, '
    '`userspace/suchu/src/gui_backend.ml`, `platform_backend.ml`, `platform_stubs.c`, '
    '`userspace/raylib/rcore_vexos.c`, `vex_input_observer.h`.',
    '', 'Mesure/build : `userspace/libvex/src/vex_bench.c`, `include/vex_bench.h`, '
    '`userspace/apps/paint_bench_runner.c`, `scripts/build-suchu.sh`, '
    '`build-suchu-paint.sh`, `build-paint-comparison.ps1`, `run-paint-comparison.ps1`, '
    '`test-paint-functions.ps1`, `paint-pixels.py`, `analyse-paint-comparison.py`, '
    '`paint-comparison-metadata.py`, `report-paint-comparison.py`.']
validation[0:0]=['Comptage des blocs d’infrastructure touchés, hors blancs/commentaires (code à jour, pas diff Git historique) :',
 '', '| Adaptation | C | OCaml |', '|---|---:|---:|',
 '| Clock CPU et sélection clocks 2/3 | 9 | 0 |',
 '| Callback monotone LVGL et branchement | 6 | 0 |',
 '| Deadline kernel, flag ABI, helper native wait | 17 | 0 |',
 '| Deadline Raylib et bridge wake | 10 | 2 |',
 '| Primitives graphiques et minimum explicite | 0 | 20 |',
 '| Total infrastructure | 42 | 22 |', '',
 'Le collecteur, le parent et les wrappers de mesure sont comptés séparément dans la section Code. Les bibliothèques vendoriées ne sont pas du nouveau code Paint.', '']
p=root/'docs/paint-comparison.md';s=p.read_text(encoding='utf-8')
s=re.sub(r'(## Résultats\n).*?(?=\n## Reproduction et données)',lambda m:m[1]+'\n'+'\n'.join(lines)+'\n',s,flags=re.S)
s=re.sub(r'(## Code, binaires et limites\n).*?(?=\nLes deux exécutables)',lambda m:m[1]+'\n'+'\n'.join(sizes)+'\n',s,flags=re.S)
s=s.split('## Validation et conclusion')[0]+'## Validation et conclusion\n\n'+'\n'.join(validation)+'\n'
p.write_text(s,encoding='utf-8')
