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
| 3c | Passe HDR générique pour toutes les variantes du composite (`BB_HDR_TONEMAP=4`, *bbport HDR, all areas*) | **Codée et vérifiée hors jeu** (16/10, build hdr-20) : 280 variantes reproduites à l'identique (émulateur du code du jeu), shader GLSL identique au modèle. **À tester en jeu** |
| — | PsychoV30 (tonemapper perceptif de RenoDX) porté en GLSL : `hdr/psychov30.glsl` | **Branché** (16/10, build hdr-21) en option de la passe générique (*HDR highlights* = PsychoV30), calé sur la courbe du jeu au gris moyen ; vérifié hors jeu. **À tester en jeu** |
| 4 | Upscalers (FSR/DLSS en mode mis à l'échelle travaillent sur l'image déjà tonemappée) | À faire |

Options (launcher → Fenêtre, ou variables d'environnement) :

| Launcher | Variable | Défaut |
|---|---|---|
| Allow HDR output | `BB_HDR=1` | off |
| HDR brightness (paper white) | `BB_HDR_PAPER_WHITE=80..500` | 203 |
| HDR test pattern | `BB_HDR_TEST_PATTERN=1` | off |
| HDR tonemap (test) | `BB_HDR_TONEMAP=0/1/2/3/4` (jeu / bbport / bbport + teinte / bbport HDR / bbport HDR toutes zones) | 0 |
| HDR highlights (all areas) | `BB_HDR_TONEMAPPER=0/1` (épaule Neutwo / PsychoV30), passe générique seulement | 0 |
| HDR peak brightness | `BB_HDR_PEAK_NITS=200..10000` | 1000 |
| HDR debug view | `BB_HDR_DEBUG=1` gain en couleurs, `=2` pixels pris pour l'interface en rouge | off |
| — | `BB_HDR_KNEE=0.3..0.95` : part du blanc sous laquelle la courbe du jeu est gardée | 0.6 |
| HDR diagnostic (collect shaders) | `BB_DUMP_SHADERS=1`, `BB_PIPELINE_CACHE=0`, `BB_CAPTURE_AUTO=20`, `BB_CAPTURE_DIR=<sauvegardes>/shader/dumps/frames` | off |
| — | `BB_HDR_GAMMA22=0` : décodage sRGB au lieu de gamma 2.2 | 1 |

Le journal (`user\last_run.log`, lignes GPU au niveau info : `BB_GPU_LOG=info`, mis d'office par le launcher
depuis hdr-22 quand un mode « HDR tonemap » est choisi) contient `HDR output: on, HDR10 swapchain` quand la sortie HDR est active,
ou `HDR output: off, the display offers no HDR10 format` quand l'écran ne propose pas le HDR10.

## Reprendre le projet (point au 16/10, après-midi)

Où on en est : le HDR natif marche (étape 3b, `5c02eaf8` seulement). La fenêtre de la clinique monte à 916 nits
au lieu de 203 en SDR. Nouveau (16/10) : la **passe générique** (étape 3c, `BB_HDR_TONEMAP=4`) couvre les 280
variantes standard du composite final sans remplacer les shaders du jeu (voir « Passe HDR générique »). Elle
réutilise la détection de l'interface de l'étape 3b.

Jeu : `E:\Bloodborne\Game\CUSA03173` (dump 1.09). bbport : `E:\Bloodborne\hdr`.

**Prochain test : build `bbport-windows-hdr-21` (ou plus récent)** — extraire l'artifact par-dessus
`E:\Bloodborne\hdr`. Launcher : *Allow HDR output* coché, *HDR peak brightness* = 1000.

A. Détection de l'interface (*HDR tonemap (test)* = **bbport HDR**, mode 3, comme prévu ce matin), devant la
   fenêtre lumineuse de la clinique puis devant une lanterne ou un feu :
   1. *HDR debug view* = **Pixels taken as UI in red** → capture JXR (Win + Alt + Impr. écran). Attendu : seule
      l'interface en rouge, ni les vitres ni les barreaux, ni les contours des objets et du personnage (le 15/10,
      presque tous les bords étaient rouges : même cause, le mélange des voisins par l'AA). Une capture caméra
      immobile, une autre en tournant la caméra : des bords rouges seulement en mouvement indiqueraient un
      décalage d'une image entre le canal et l'image affichée, pas un problème d'AA.
   2. *HDR debug view* = **Off** → capture JXR au même endroit (mesure des nits).
B. Passe générique (*HDR tonemap (test)* = **bbport HDR, all areas**, mode 4), mêmes endroits :
   1. *HDR debug view* = **Off** → capture JXR. Attendu : même image qu'en mode 3 (même courbe, même genou).
   2. *HDR debug view* = **Highlight gain as colors** → capture : lampes, fenêtres et ciel colorés, le reste gris.
   3. Jouer dans plusieurs zones (rêve du chasseur, Yharnam, Cathédrale, Forêt interdite, Byrgenwerth,
      cauchemars, donjons des calices si possible). Si une zone paraît fausse (couleurs, contraste différents
      du SDR), une capture JXR là, en mode 4 puis en mode **Game**.
   4. Quitter et envoyer `last_run.log` (lignes `HDR pass:`, `HDR tonemap variant` et `HDR channel:`).
      « HDR pass » = couvert ; « NOT covered » ou « left in SDR » = à regarder.
C. PsychoV30 (mode 4 et *HDR highlights (all areas)* = **PsychoV30**), aux mêmes endroits que B :
   capture JXR en *HDR debug view* = Off. À comparer avec B.1 : couleurs des lumières (flammes, lampes),
   contraste et teinte générale (PsychoV refait toute la courbe, pas seulement les hautes lumières).
D. Envoyer les `.jxr` (dossier `Vidéos\Captures`).

En cas de problème en mode 4 (image fausse, plantage) : revenir au mode 3 ou 0 et envoyer `last_run.log`.

Analyse des captures : `docs/hdr-tools/jxr_nits.py` (mode d'emploi en tête du fichier : compiler jxrlib, décoder
en `-c 25`, surtout pas `-c 28` qui donne une image zoomée fausse).

Ensuite, dans l'ordre :
- passe générique validée : PsychoV30 en option à côté de Neutwo (couleur HDR par canal au lieu d'un gain de
  luminance), puis les 16 variantes à effets (plusieurs échantillons de la scène) si le journal en montre ;
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
  dans `gpu/shadps4/video_core/renderer_vulkan/bb_tonemap_variants.h` (régénérée par
  `bb_composite_model.py <export> <header>` avec les options vérifiées, voir ci-dessous). Courbes, `x` = scène
  exposée (plancher 6,1e-5) :
  - **exp** : `y = 1 − exp(−k·x)`, `T = y·(c·(y−1)+1)²` (k = mot 832, c = 833) — dont `5c02eaf8` ;
  - **Reinhard étendu** : `u = a·x`, `T = u·(1 + b·u)/(1 + u)` (a = mot 833, b = 834) ;
  - **log** : `T = a·ln(1 + b·x)` (a = 833, b = 834) ;
  - **aucune** : écrêtage.
  Les briques Filmic / Tabled de `gxposteffect` ne sont pas dans le composite. Exemples : exp `0b30a83f`,
  Reinhard `02fe4979`, log par canal `40ed231a`, log sur luminance `093fc8fa`.
- Les dumps du 15/10 (clinique, rues, rêve du chasseur) n'ont vu que `5c02eaf8`. `HdrChannel::NoteDraw` écrit
  `HDR tonemap variant <hash> in use … <état>` à la première utilisation de chaque variante.

### Passe HDR générique (étape 3c, codée le 16/10)

**Élargie encore (build hdr-26) : 1 380 variantes.** Le journal de hdr-24 (`HDR scan:`, partie lancée au Rêve
du chasseur, carte m21) montre le composite du Rêve : `5e67d866`, dessiné avec le même vertex shader `1ea5a07e`,
**sans matrice de couleurs** (mots 840–851 absents : la sortie est le mélange écran lui-même). Nouveau critère :
les teintes (mots 744 et 752) en plus des précédents ; les shaders trouvés par les teintes seules ne sont gardés
que s'ils sont reproduits (d'autres passes, comme le flou de mouvement `e0305cef`, lisent aussi ces mots).
Relevé : **1 380 reproduites** (écart max 0,019), 38 laissées en SDR dans la table (16 distorsions, 4 triviales,
18 à matrice et pré-exposition). GLSL contre modèle : 3,7e-5 sur 353 280 pixels, 0 violation de la courbe HDR ;
PsychoV30 : 1 cas sur 245 696 où, au pic, la luminance moyenne baisse de 0,3 % quand la saturation est comprimée
(comportement de PsychoV30). La version du launcher affiche maintenant le build (`v1.7.8 · build hdr-N`, aussi en
première ligne de `last_run.log`).

**Élargie le 16/10 au soir (build hdr-24) : 715 variantes au lieu de 280.** Le premier relevé exigeait le mot
616 (gain de la scène) ; le jeu a deux autres familles, reconnues par les mots 840–851 : sans gain (gain 1), et
avec une **matrice 3×4 sur la scène teintée** (mots 856–867, vignettages appliqués après) et **une sur le halo**
(mots 872–883, `max(·, 0)` au lieu de `c617`), certaines sans plancher 2^-14 avant la courbe. Le Rêve du chasseur
n'était pas couvert (en jeu : canal HDR figé, puis absent). Relevé : 748 variantes, **715 reproduites** (écart
max 0,018), 33 laissées en SDR (16 distorsions, 4 triviales, 13 à matrice et pré-exposition reproduites à plus de
0,02). GLSL contre modèle : 2,1e-5 sur 183 040 pixels, 0 violation HDR et PsychoV30. Depuis hdr-23, une zone dont le
composite n'est pas couvert reste en SDR (plus de canal figé), et le journal liste les passes plein écran
(`HDR scan:`) avec `BB_GPU_LOG=info`.

**Relevé exact des variantes.** Correction : le code d'un shader commence à `s_mov_b32 vcc_hi, n`
(`0xBEEB03FF`), le pied `OrbShdr` étant `(n + 1) × 2` mots plus loin (shadPS4 `SearchBinaryInfo`) ; l'export
`eboot_shaders.bin` contient assez d'octets avant le pied. `gcn_emu.py` exécute le code GCN d'une variante
(une voie, flottants 32 bits) ; `bb_composite_model.py` est le modèle de bbport et trouve pour chaque variante
les options qui reproduisent ce code : **280 variantes standard reproduites** (écart max 0,012 sur un pixel
quasi noir, tous les autres < 1/255), 20 à part (16 à plusieurs échantillons de la scène : distorsions ; 4
triviales). Structure (« uber-shader ») :

- textures : scène (RGBA HDR, attribut 0), **multiplicateur 1×1** (attribut 2 ou 3) qui sert deux fois — teinte
  de la scène `ms = tint(752, 753, 756)·c754 + c755` et base du bloom `mb = clamp(tint(744, 745, 748))·c746 + c747`
  —, bloom (attribut 1), grain (attribut 3 ou 4) ;
- `x = pré-exposition(scène) × c616 × vignettages × ms`, puis courbe, puis `S = T + U − T·U` avec
  `U = clamp(c617·(mb + bloom·ms))`, matrice 3×4, gamma `c808` ou sortie linéaire, grain `c809·g + c810` ;
- courbes : exp (40), Reinhard étendu (80), log (80), aucune (60), par canal ou sur la **moyenne RGB** (80) ;
  en Reinhard sur luminance la valeur de la courbe n'est pas bornée, en log elle l'est ;
- pré-expositions : `c19·(v + √(v(v + c16) + 1) − 1)` (80), `c11·(e^(c8·v) − 1)` (40), scène bornée ×
  `c12·(c14·a² + 1)` (alpha de la scène, 20) ; par canal ou sur la moyenne ;
- vignettages : `(1 − r²)⁴` (112) et objectif `smoothstep(c888·r + c889)·(1/(1 + (c890·r)²))^c892` (56), sur
  l'attribut 2 ;
- 20 variantes **additives** (sans courbe ni borne, bloom ajouté, sortie flottante) : HDR passé à une passe
  suivante ; la passe générique les laisse (gain 1).

**Implémentation** (`BB_HDR_TONEMAP=4`, launcher *bbport HDR, all areas*) :

- `hdr/bb_composite.glsl` : le composite (sortie SDR identique au modèle, vérifié par
  `docs/hdr-tools/bb_composite_check/run.sh` : écart 1,4e-5 sur 71 680 pixels × 280 variantes) et sa version HDR :
  courbe du jeu sous le genou, épaule Neutwo de même pente au-dessus ; pour un Reinhard convexe, genou au point où
  la courbe atteint le blanc ; sur luminance, borne au pic par le canal le plus fort ; jamais sous la valeur SDR.
- `hdr/bb_composite_hdr.frag` : même sortie que `bb_tonemap_hdr.frag` (gain, luminance encodée, linéaire).
- Les shaders du jeu ne sont pas remplacés. `GameShaderCode` passe le SPIR-V de chaque shader à
  `HdrChannel::NoteGameShader` : pour une variante de la table, bbport lit dans ce SPIR-V les liaisons de
  `fs_img*` / `fs_samp*`, l'emplacement de `fs_in_attr*_p` et le tampon `ssbo_N`, les recopie sur les
  décorations `bb_*` du shader HDR et fixe les options en constantes de spécialisation. Le pipeline du canal est
  ensuite créé comme en mode 3 (`vk_graphics_pipeline.cpp`), le dessin fantôme et le post-process sont inchangés.
- Refus (journal `HDR pass: … left in SDR`) : plusieurs tampons, nombre d'images ≠ nombre de samplers, entrées
  non interpolées par sommet, permutations d'interfaces différentes.

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
- Branchement (16/10) : passe générique, `BB_HDR_TONEMAPPER=1`. PsychoV remplace la courbe du jeu sur la scène
  exposée `x`, avant bloom et matrice ; ancrage `anchor-output-midgray` + `slope-analytic` : l'entrée où la
  courbe du jeu donne 0,18 (`bb_knee(…, 0.18)`) donne 0,18, contraste = pente log-log du jeu à ce point (borné
  0,3–3), cube BT.709 (sortie sans canal négatif). Vérifié par `bb_composite_check` (gris conservé, croissant,
  sous le pic, 0 écart sur 50 304 pixels × variantes).
- Format du canal (16/10) : r, b, a = gains du rouge, du vert, du bleu, g = luminance de référence. Neutwo écrit
  le même gain trois fois ; PsychoV un gain par canal (rangées de la matrice ; luminance là où le canal du jeu
  est presque nul ; borné 0–64). Le post-process les applique en BT.709 linéaire avant BT.2020. Effacement du
  canal à (1, 0, 1, 1).

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
