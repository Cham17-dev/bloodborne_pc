# HDR pour Bloodborne (branche `hdr`)

## État

| Étape | Contenu | État |
|---|---|---|
| 1 | Sortie HDR10 (Rec.2020 + PQ) de l'image SDR du jeu, paper white réglable, menu Insert en PQ | **Validé** (15/10, MSI QD-OLED 4K, RTX 4090) : image identique au SDR, paper white OK |
| — | Mire de test HDR (`BB_HDR_TEST_PATTERN=1`) | **Validé** : ombres distinctes, couleurs justes, écrêtage vers 1000 nits (réglage de l'écran) |
| — | Mode « HDR diagnostic » : export des shaders (cache désactivé) + analyse d'une image toutes les 20 s, constantes sur 4 Ko, textures par dessin | Validé |
| 2 | Identifier le shader de tonemap du jeu | **Fait** — voir « Tonemap de Bloodborne » ci-dessous |
| 3a | Remplacer le shader de tonemap (`hdr/bb_tonemap.frag`, `GameShaderCode()`, option *HDR tonemap (test)*, `BB_HDR_TONEMAP=0/1/2`) | **Validé** (15/10) : mode 2 teinte la partie gauche (environ un quart de l'écran : la cible fait plus de 1920 px de large en sortie 4K, à prendre en compte), mode 1 donne une image identique au jeu. Le cache de pipelines est contourné (remplacement aussi au chargement depuis le cache). |
| 3b | Canal HDR : dessin fantôme du tonemap dans une image RGBA16F (gain HDR par pixel) + application du gain dans le post-process final là où l'image est encore la scène | **Codé le 15/10 au soir, compile, à tester** (*HDR tonemap (test)* = *bbport HDR*, `BB_HDR_TONEMAP=3`) |
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
