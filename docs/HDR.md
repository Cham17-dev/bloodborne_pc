# HDR pour Bloodborne (branche `hdr`)

## État

| Étape | Contenu | État |
|---|---|---|
| 1 | Sortie HDR10 (Rec.2020 + PQ) de l'image SDR du jeu, paper white réglable, menu Insert en PQ | **Validé** (15/10, MSI QD-OLED 4K, RTX 4090) : image identique au SDR, paper white OK |
| — | Mire de test HDR (`BB_HDR_TEST_PATTERN=1`) | **Validé** : ombres distinctes, couleurs justes, écrêtage vers 1000 nits (réglage de l'écran) |
| — | Mode « HDR diagnostic » : export des shaders (cache désactivé) + analyse d'une image toutes les 20 s, constantes sur 4 Ko, textures par dessin | Validé |
| 2 | Identifier le shader de tonemap du jeu | **Fait** — voir « Tonemap de Bloodborne » ci-dessous |
| 3a | Remplacer le shader de tonemap (`hdr/bb_tonemap.frag`, `GameShaderCode()`, option *HDR tonemap (test)*, `BB_HDR_TONEMAP=0/1/2`) | **Validé** (15/10) : mode 2 teinte la partie gauche (environ un quart de l'écran : la cible fait plus de 1920 px de large en sortie 4K, à prendre en compte), mode 1 donne une image identique au jeu. Le cache de pipelines est contourné (remplacement aussi au chargement depuis le cache). |
| 3b | Canal HDR : dessin fantôme du tonemap dans une image RGBA16F (gain HDR par pixel) + application du gain dans le post-process final là où l'image est encore la scène | **Fonctionne** (15/10, build hdr-15) : fenêtre de la clinique jusqu'à 916 nits, pas de plantage, interface normale. **En cours** : détection de l'interface (barreaux de fenêtre encore pris pour l'interface à cause de l'AA → correctif 3×3, build hdr-16, à tester) |
| 3c | Couvrir toutes les variantes du composite final (300 dans `eboot.bin`, voir « Variantes du tonemap ») | **Relevé fait** (16/10). Build hdr-18 : journal des variantes utilisées en jeu. Remplacement des autres variantes : selon ce que le journal montre |
| — | PsychoV30 (tonemapper perceptif de RenoDX) porté en GLSL : `hdr/psychov30.glsl` | **Porté et vérifié hors jeu** (16/10) : identique à l'original HLSL à 1,5e-4 près. À brancher avec la passe générique (option à côté de Neutwo) |
| 4 | Upscalers (FSR/DLSS en mode mis à l'échelle travaillent sur l'image déjà tonemappée) | À faire |

Options (launcher → Fenêtre, ou variables d'environnement) :

| Launcher | Variable | Défaut |
|---|---|---|
| Allow HDR output | `BB_HDR=1` | off |
| HDR brightness (paper white) | `BB_HDR_PAPER_WHITE=80..500` | 203 |
| HDR test pattern | `BB_HDR_TEST_PATTERN=1` | off |
| HDR tonemap (test) | `BB_HDR_TONEMAP=0/1/2/3` (jeu / bbport / bbport + teinte / bbport HDR) | 0 |
| HDR peak brightness | `BB_HDR_PEAK_NITS=200..10000` | 1000 |
| HDR debug view | `BB_HDR_DEBUG=1` gain en couleurs, `=2` pixels pris pour l'interface en rouge | off |
| — | `BB_HDR_KNEE=0.3..0.95` : part du blanc sous laquelle la courbe du jeu est gardée | 0.6 |
| HDR diagnostic (collect shaders) | `BB_DUMP_SHADERS=1`, `BB_PIPELINE_CACHE=0`, `BB_CAPTURE_AUTO=20`, `BB_CAPTURE_DIR=<sauvegardes>/shader/dumps/frames` | off |
| — | `BB_HDR_GAMMA22=0` : décodage sRGB au lieu de gamma 2.2 | 1 |

Le journal (`last_run.log`) contient `HDR output: on, HDR10 swapchain` quand la sortie HDR est active,
ou `HDR output: off, the display offers no HDR10 format` quand l'écran ne propose pas le HDR10.

## Reprendre le projet (point au 16/10, 9 h)

Où on en est : le HDR natif marche (étape 3b). La fenêtre de la clinique monte à 916 nits au lieu de 203 en SDR.
Il reste à affiner quels pixels sont « la scène » (amplifiés) et lesquels sont « l'interface » (laissés en SDR).
Nouveau (16/10) : le composite final existe en 300 variantes (voir « Variantes du tonemap ») ; seule
`5c02eaf8` est remplacée. Le build hdr-18 écrit dans `last_run.log` chaque variante que le jeu utilise.

Jeu : `E:\Bloodborne\Game\CUSA03173` (dump 1.09). bbport : `E:\Bloodborne\hdr`.

**Prochain test (build `bbport-windows-hdr-18`, commit `43ebf46` ; le canal HDR est celui de hdr-16, comparaison sur 3×3
texels)** — devant la fenêtre lumineuse de la clinique (là où on se réveille), puis devant une lanterne ou un
feu :

1. Extraire l'artifact par-dessus `E:\Bloodborne\hdr`. Launcher : *Allow HDR output* coché, *HDR tonemap (test)*
   = **bbport HDR**, *HDR peak brightness* = 1000.
2. *HDR debug view* = **Pixels taken as UI in red** → capture JXR (Win + Alt + Impr. écran). Attendu : seule
   l'interface en rouge, ni les vitres ni les barreaux, ni les contours des objets et du personnage (le 15/10,
   presque tous les bords étaient rouges : même cause, le mélange des voisins par l'AA). Une capture caméra
   immobile, une autre en tournant la caméra : des bords rouges seulement en mouvement indiqueraient un
   décalage d'une image entre le canal et l'image affichée, pas un problème d'AA.
3. *HDR debug view* = **Off** → capture JXR au même endroit (mesure des nits).
4. Même chose en *HDR tonemap (test)* = **bbport** (sans HDR) pour comparer.
5. Envoyer les `.jxr` (dossier `Vidéos\Captures`).
6. Ensuite jouer normalement dans plusieurs zones (rêve du chasseur, Yharnam, Cathédrale, Forêt interdite,
   Byrgenwerth, cauchemars, donjons des calices si possible), quitter, et envoyer les lignes
   `HDR tonemap variant` de `last_run.log`. Une ligne « NOT replaced » signale une zone restée en SDR.

Analyse des captures : `docs/hdr-tools/jxr_nits.py` (mode d'emploi en tête du fichier : compiler jxrlib, décoder
en `-c 25`, surtout pas `-c 28` qui donne une image zoomée fausse).

Ensuite, dans l'ordre :
- si des barreaux ou bords restent rouges : capturer la LUT de luminosité de la copie d'affichage (`38d65b32`)
  pour une détection exacte, ou comparer à l'image juste avant l'interface (sortie de la copie `57f3b8c2`) ;
- régler le seuil (`BB_HDR_KNEE`, 0.6) et le pic selon le rendu (ciel, lanternes, feu) ;
- vérifier l'absence de décalage d'une image en mouvement rapide (caméra qui tourne devant une fenêtre) ;
- étape 4 (FSR/DLSS en mode mis à l'échelle), traductions du launcher, fusion de Supermedo 1.8.1 (4 commits).

## Tonemap de Bloodborne (relevé le 15/10)

Passe `#…` à 2 dessins, cible `R8G8B8A8Unorm 1920x1080` (la cible de l'interface) :

- **Dessin 1** : vs `1ea5a07e`, ps `5c02eaf8`. Textures : `img0` 1×1 RGBA8 (multiplicateur neutralisé par les
  constantes : x = 1), `img1` **la scène HDR RGBA16F** (image remplacée par un proxy de bbport, donc absente des
  listes de l'analyseur), `img2` bloom `B10G11R11 628x530`, `img3` grain 4×4.
  Constantes (tampon slot 0, mots) : 616 = 1,5 (gain de la scène) · 832 = k (2,30 clinique, 1,97 extérieur) ·
  833 = c genou (0,46 / 0,33) · 840–851 matrice 3×4 de désaturation (varie par lieu) · 808 = 1/2,2 · 809 = 1/255 et
  810 = −0,5/255 (grain) · 617 = 1 · 744–756 : chemins neutralisés.
  Calcul : `v = scène × 1,5` ; `y = 1 − exp(−k·v)` ; `T = y·(c·(y−1)+1)²` (clampé 0–1) ; bloom `U = clamp(bloom)` ;
  écran `T + U − T·U` ; matrice couleur ; `pow(·, 1/2,2)` ; grain. Sortie en **gamma 2,2** (confirme le décodage
  2,2 de l'étape 1). Alpha = scène.a.
- **Dessin 2** : vs `0b0acf50`, ps `ed4c7668` : LUT 3D 16³ (`R32G32B32A32 16x16`, préparée par cs `200820a1`) sur
  `0x10508e0000` (RGBA8). Rôle exact à confirmer (quad de 4 indices).
- Après : copie `f7229091` vers `0x10508e0000`, AA du jeu `da1faca9` vers la cible UI, copie `57f3b8c2`, puis
  l'interface ; au début de l'image suivante, copie de la cible UI vers le tampon d'affichage (RGBA8 sRGB).

## Variantes du tonemap (relevé statique du 16/10)

Analyse hors jeu des shaders du dump 1.09 (outils dans `docs/hdr-tools/`) :

- `dvdroot_ps4/shader/*.shaderbnd.dcx` (DCX zlib + BND4, noms en clair, `bnd_extract.py`) : effets nommés.
  `gxposteffect` contient les briques `ToneMap_*` (Exp, Filmic, Reinhard, Tabled, TabledLUT, Divide, Gamma,
  None…) et identifie des hashes mesurés en jeu : `ed4c7668` = **ColorGrading** (le 2e dessin de la passe :
  LUT d'étalonnage), `da1faca9` = **DLAA** (AA du jeu), `57f3b8c2` = EmissiveToGlowPS, `f7229091` =
  PointSampleColor1, `200820a1` = LutCopy2DTo3DCs. `gxflvershader` (14 571 shaders) = matériaux des modèles.
- Le tonemap `5c02eaf8`, son vs `1ea5a07e` et la copie d'affichage `38d65b32` sont **dans `eboot.bin`**
  (5 469 shaders embarqués ; hash = `shader_hash` du pied `OrbShdr`, celui de shadPS4).
- `bb_shader_scan.py` trouve le composite final par ses constantes (mots 616 et 840–851) : **300 variantes**,
  générées dans `gpu/shadps4/video_core/renderer_vulkan/bb_tonemap_variants.h`. Désassemblage
  (`gcn_disasm.py`), avec `x` = scène × gain (mot 616) × réglages 744–756, plancher 6,1e-5 :
  - **0 exp** (40, dont `5c02eaf8`) : `y = 1 − exp(−k·x)`, `T = y·(c·(y−1)+1)²` (k = mot 832, c = 833) ;
  - **1 Reinhard étendu** (80) : `u = a·x`, `T = u·(1 + b·u)/(1 + u)` (a = mot 833, b = 834 ; point blanc) ;
  - **2 log** (80) : `T = a·ln(1 + b·x)` (b = mot 834, a = 833) ;
  - **3 aucune** (100) : écrêtage.
  Chaque courbe existe par canal ou **sur la luminance** (couleur mise à l'échelle), avec ou sans
  **vignettage** (mots 890–892 : `pow(1/(1+(k·r)²), p)`), avec ou sans gamma 2,2 (mot 808), grain (809–811),
  et 2 à 7 textures (bloom, etc.). Les briques Filmic / Tabled de `gxposteffect` ne sont pas dans le composite.
  Exemples : exp `0b30a83f`, Reinhard `02fe4979`, log par canal `40ed231a`, log sur luminance `093fc8fa`.
- Les dumps du 15/10 (clinique, rues, rêve du chasseur) n'ont vu que `5c02eaf8`. Build hdr-18 :
  `HdrChannel::NoteDraw` écrit `HDR tonemap variant <hash> in use … replaced by bbport / NOT replaced` à la
  première utilisation de chaque variante.
### Plan pour couvrir les 300 variantes (étape 3c)

Le dessin fantôme actuel réutilise le layout de `5c02eaf8` : un shader maison par layout de variante serait
ingérable. À la place, **une passe à nous** au moment d'un dessin de la famille (après le dessin du jeu) :

1. Dans `DrawRecord`, si le ps est dans `TonemapVariants` : retrouver parmi les ressources liées l'image de la
   scène (RGBA16F), le bloom (B10G11R11, s'il y en a un) et le tampon de constantes du composite (celui que le
   shader lit aux mots 616…892).
2. Dessiner un triangle plein écran avec un pipeline et un jeu de descripteurs à bbport (scène, bloom,
   constantes) dans l'image du canal HDR. Le shader reçoit en push constants la courbe, `luminance`,
   `vignette`, `gamma` de la variante, recalcule exactement la sortie SDR du jeu (courbe + bloom en écran +
   matrice + gamma ; grain ignoré, couvert par la tolérance) et la version HDR (courbe du jeu jusqu'au genou,
   épaule Neutwo au-dessus), puis écrit gain + luminance de référence comme aujourd'hui.
3. Le post-process final reste identique.

Avantages : indépendant du layout de chaque variante, une seule implémentation par courbe. À vérifier : que la
scène et le bloom se reconnaissent bien par leur format dans toutes les variantes (journal), et l'UV (le jeu
interpole des attributs ; plein écran = `gl_FragCoord / taille`, à confirmer par la comparaison au SDR).

## PsychoV30 (porté le 16/10)

Tonemapper perceptif de RenoDX (Carlos Lopez, MIT), `src/shaders/tonemap/psychov/test30.hlsl` au commit
`0ede35a` : réponse des cônes (LMS Stockman/CVRL), courbe finie par cône ancrée (gris moyen, pente), direction
« Mean-A2 », projection exacte dans le cube RGB de l'écran (BT.709 ou BT.2020). Garde teinte et saturation des
hautes lumières là où un gain de luminance les laisse délavées.

- Port : `gpu/shadps4/video_core/host_shaders/hdr/psychov30.glsl`, fonctions `rd_psychov30(...)` (tous les
  réglages) et `rd_psychov30(couleur, pic)` (défauts RenoDX : ancres 0,18, cube BT.2020, h = 1). Entrée et sortie
  en BT.709 linéaire, 1 = paper white ; `pic` = pic ÷ paper white (1000/203 = 4,93).
- Constantes dérivées calculées en double par `docs/hdr-tools/psychov30_consts.py`.
- Vérification : `docs/hdr-tools/psychov30_check/run.sh` compile l'original HLSL et le port avec glslang, les
  traduit en C++ (SPIRV-Cross) et compare 768 couleurs × 7 réglages : écart max 1,5e-4 (1,3e-3 pour une couleur
  très hors gamut en source AP1, mode non utilisé). Compilation aussi vérifiée en CI (`psychov30_check.frag`).
- Points de repère (défauts, pic 4,93) : gris 0,215 → 0,215 ; 1,67 → 1,36 ; 10 → 3,5 ; 1000 → 4,91 ;
  rouge pur 7,4 → (5,41 0,15 0,07) ; rouge pur 1000 → (5,16 4,55 4,44), il tend vers le blanc.
- Branchement : dans la passe générique (étape 3c), sur la couleur de la scène, avant l'étalonnage du jeu.
  Ancrage sur la courbe du jeu : sortie au gris moyen et pente calculées avec les formules exactes des 4
  courbes (méthode RenoDX `anchor-output-midgray` + `slope-analytic`).

## Plan de l'étape 3b (rédigé le 15/10 au soir)

### Le problème

Après le tonemap, l'image passe par 4 étapes en 8 bits (copie, AA du jeu, copie, interface) puis par le tampon
d'affichage (RGBA8 sRGB) : tout ce qui dépasse le blanc y serait écrêté.

### Option écartée : passer ces images en 16 bits

Il faudrait un système de substitution comme les proxys de `SceneTargets` (`vk_scene_resolution.*`, environ
500 lignes), mais pour le format au lieu de la taille. Or les pipelines du jeu sont créés pour des attachements
RGBA8 : dessiner l'interface ou l'AA dans une image RGBA16F demande d'autres pipelines, et une vue RGBA8 d'une image
RGBA16F n'est pas permise (classes de taille différentes). Les copies avec inversion BGR, l'AA, l'interface, le
tampon d'affichage et les relectures CPU seraient tous concernés : trop invasif et fragile.

### Option retenue : un « canal HDR » à côté de la chaîne SDR

On ne touche pas à la chaîne 8 bits du jeu : elle continue de produire l'image SDR, interface comprise.

1. **Dessin fantôme** : juste après le dessin du tonemap (ps `5c02eaf8`), le rasterizer rejoue ce même dessin
   (mêmes descripteurs, mêmes constantes, même vertex shader, mêmes 3 sommets) avec un second pipeline : même
   layout, fragment shader `hdr/bb_tonemap_hdr.frag`, attachement RGBA16F appartenant à bbport, de la taille de la
   cible du tonemap. Ce shader calcule, avec les formules de `bb_tonemap.frag` :
   - la sortie SDR d'origine (référence, encodée comme celle du jeu), en alpha ou dans un canal dédié ;
   - la couleur HDR : même étalonnage dans les tons moyens, hautes lumières prolongées (Neutwo /
     `rd_upgrade_tonemap` de `hdr/renodx.glsl`), en linéaire, relative au paper white.
2. **Recomposition à l'affichage** : le post-process final (`post_process.frag`, chemin HDR de l'étape 1) lit
   l'image SDR finale **et** l'image du canal HDR. Là où le pixel final correspond encore à la sortie SDR de
   référence (à une tolérance près, pour l'AA), c'est la scène : on affiche la version HDR. Ailleurs, c'est
   l'interface (ou un mélange) : on garde le SDR. Les menus et le texte restent donc au paper white.
3. Vérifier que l'image du canal HDR est lue avant d'être réécrite par l'image suivante (copie ou double tampon
   si besoin), et gérer la taille (cible du tonemap : 1920×1080 ou plus en sortie 4K ; FSR mis à l'échelle).

Points à vérifier en codant :
- rejouer un dessin avec un pipeline maison dans `Rasterizer::DrawRecord` / `PostDraw` (`vk_rasterizer.cpp`) :
  descripteurs et push constants déjà liés, rendu dynamique ;
- le rôle du dessin 2 de la passe (LUT `ed4c7668`) : le test du 15/10 (teinte visible, image identique) montre
  qu'il ne recouvre pas le dessin 1 ;
- la tolérance de détection de l'interface face à l'AA du jeu (`da1faca9`) et au grain.

Mesures du 15/10 (captures JXR décodées) : fenêtre de la clinique jusqu'à 916 nits (cœur 560–700, halo 200–450) ;
dehors au crépuscule, ciel 13–30 nits, rien au-dessus du seuil (122 nits) : normal. Détection de l'interface :
tolérance croissante avec la luminosité (`0.04 + 0.22·g²`), puis comparaison à la plage min–max du canal sur 3×3
texels (l'AA mélange les voisins : les barreaux de fenêtre étaient pris pour l'interface).

Réglages prévus : luminosité max (nits, défaut selon l'écran), paper white (existe déjà), intensité des hautes
lumières.

**Fait le 15/10 au soir** : `hdr/bb_tonemap_hdr.frag` (pas encore branché, compilé avec le projet). Courbe : celle du
jeu jusqu'au genou (`BB_HDR_KNEE`, 0,6 du blanc ≈ 122 nits à 203 de paper white), puis épaule Neutwo de même pente
qui monte vers `BB_HDR_PEAK` (pic ÷ paper white, 1000/203 = 4,93). Vérifiée en Python avec les constantes
capturées (clinique k = 2,30 c = 0,46 ; extérieur k = 1,97 c = 0,33) : identique au jeu sous le genou, sans saut ni
cassure de pente, croissante, ~985 nits au maximum. Sortie : rgb HDR linéaire (1 = paper white), a = luminance de
la sortie SDR du jeu (pour distinguer la scène de l'interface).
À régler en le branchant : l'ordre des canaux (le jeu écrit la ligne 848 de la matrice dans le premier canal et
les copies suivantes inversent B et R), et la façon de passer les spécialisations (`SetSpecConstant`, ids 1 et 2,
flottants).

## Implémentation de l'étape 3b (15/10 au soir)

- `vk_hdr_channel.*` : deux images RGBA16F en alternance à la taille de la cible du tonemap, le module du shader
  HDR (spécialisations pic ÷ paper white et genou), les réglages.
- `vk_graphics_pipeline.cpp` : pour le pipeline du tonemap remplacé (`GameShaderReplaced`), une copie avec le shader
  HDR et un attachement RGBA16F sans mélange (`HdrHandle()`).
- `vk_rasterizer.cpp` (`DrawRecord`) : après le dessin du tonemap, fin de la passe du jeu, dessin fantôme dans
  l'image du canal (effacée à gain 1), barrières, puis reprise de la passe du jeu en `Load`. L'image est préparée
  avant `BeginRendering` (un redimensionnement attend le GPU).
- `hdr/bb_tonemap_hdr.frag` : sortie r = gain (luminance HDR ÷ luminance du jeu, 1 sous le genou), g = luminance de
  la sortie du jeu (encodée 2,2), b = luminance linéaire du jeu.
- `post_process.frag` + `pp_pass` (binding 1) : gain appliqué là où la luminance finale (après AA, grain, courbe de
  luminosité du jeu, interface) reste à ±0,03–0,10 de celle du canal ; ailleurs SDR. Écrêté au pic.
- Pourquoi un gain et pas une couleur : la copie vers l'affichage applique une courbe 1D par canal (réglage de
  luminosité du jeu) et l'ordre des canaux s'inverse plusieurs fois (tonemap BGR, copies, AA) ; un gain de
  luminance appliqué à l'image finie garde tout le rendu du jeu.

Limites connues à vérifier au premier test : décalage d'une image possible entre le canal et l'image affichée
(deux images en alternance, pas de synchronisation fine avec l'affichage), interface sur les hautes lumières,
réglage de luminosité du jeu très éloigné du défaut (la détection retombe alors en SDR).

### Test de l'étape 3b

1. *Allow HDR output* coché, *HDR tonemap (test)* = **bbport HDR**, *HDR peak brightness* = luminosité max de
   l'écran, *HDR debug view* = Off. Lancer, aller dehors (ciel, lampadaires) et près d'une lampe ou d'un feu.
   Attendu : tons moyens identiques au SDR, hautes lumières nettement plus intenses, interface normale.
2. *HDR debug view* = **Highlight gain as colors** : gris = SDR, bleu → rouge = hautes lumières amplifiées. Les
   lampes et le ciel doivent être colorés, le reste gris.
3. *HDR debug view* = **Pixels taken as UI in red** : seuls l'interface et le texte doivent être rouges.
4. En cas de problème : `last_run.log` (lignes `HDR channel:` et `HDR tonemap:`), une photo, et revenir à *bbport*.

## Fiche de test

Préparation : HDR activé dans Windows (Paramètres → Affichage → Utiliser le HDR), version de test
de la branche `hdr` (onglet Actions du fork → dernière « Windows build » réussie → Artifacts).

1. **Le jeu tourne-t-il ?** Case HDR décochée. Lancer, charger une partie, jouer une minute.
2. **Mire de test.** Cocher *Allow HDR output* et *HDR test pattern*, lancer. Quatre bandes, de haut en bas :
   1. dégradé de gris en 32 marches, du noir au blanc ;
   2. 10 carrés gris : 0,1 · 1 · 10 · 50 · 100 nits · paper white · 300 · 400 · 600 · 800 nits ;
   3. 10 carrés gris : 1000 · 1200 · 1500 · 2000 · 2500 · 3000 · 4000 · 6000 · 8000 · 10000 nits ;
   4. barres de couleur : blanc, jaune, cyan, vert, magenta, rouge, bleu, noir.

   À noter : les marches du dégradé sont-elles toutes distinctes (surtout dans le noir) ? À partir de
   quel carré les carrés suivants paraissent-ils identiques (c'est la luminosité max de l'écran) ?
   Les couleurs sont-elles franches, sans teinte bizarre ?
3. **Le jeu en HDR.** Décocher la mire, garder *Allow HDR output*. Comparer avec la case décochée :
   l'image doit être pareille qu'en SDR (ni délavée, ni trop sombre). Ouvrir le menu Insert : lisible,
   pas éblouissant ? Ajuster *paper white* si le jeu paraît terne ou trop lumineux, noter la valeur gardée.
4. **Mode diagnostic.** Cocher *HDR diagnostic*, lancer, jouer 2–3 minutes en passant par plusieurs
   endroits (rêve du chasseur, une zone extérieure, un intérieur sombre). Quitter, décocher l'option.
   Compresser le dossier `shader/dumps` du dossier de sauvegardes et l'envoyer.

À remonter : les observations des points 2 et 3, le `last_run.log` si quelque chose cloche, le zip du point 4.
