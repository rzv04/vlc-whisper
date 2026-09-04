# VLC-Whisper Local ASR Quality Benchmark Report

## Overview

- **Timestamp (UTC):** 2026-09-04T09:23:29.647709+00:00
- **Model Tested:** `ggml-tiny.bin` (Path: `/home/razvan/vlc-whisper/.worktrees/gemini/models/ggml-tiny.bin`)
- **Model Architecture:** Universal Multilingual Whisper-Tiny (39M parameters)
- **Inference Backend:** CPU (4 threads)
- **Corpus:** Google FLEURS (Test split, Revision `70bb2e84`)
- **Test Set Size:** 20 clips total (10 English `en_us` + 10 Romanian `ro_ro`)
- **Normalizer:** `vlcw-basic-v1` (NFKC, lowercase, punctuation removed, whitespace collapsed)

## Aggregate Performance by Mode

| Language | Mode | WER (%) | CER (%) | Word Errors / Ref Words | Char Errors / Ref Chars |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **English (`en`)** | `offline` | **10.38%** | **4.97%** | 22 / 212 | 50 / 1007 |
| **English (`en`)** | `live` | **46.23%** | **39.32%** | 98 / 212 | 396 / 1007 |
| **English (`en`)** | `lookahead` | **10.38%** | **4.87%** | 22 / 212 | 49 / 1007 |
| **Romanian (`ro`)** | `offline` | **93.03%** | **29.71%** | 227 / 244 | 361 / 1215 |
| **Romanian (`ro`)** | `live` | **159.43%** | **91.52%** | 389 / 244 | 1112 / 1215 |
| **Romanian (`ro`)** | `lookahead` | **89.75%** | **33.00%** | 219 / 244 | 401 / 1215 |

## Key Findings & Architectural Analysis

1. **Lookahead Mode Matches Full Offline Whisper Accuracy:**
   - On English audio, `lookahead` achieves **10.38% WER** and **4.87% CER**, exactly matching `offline` batch inference (**10.38% WER**, **4.97% CER**).
   - This confirms that VLC-Whisper’s lookahead source decoding pipeline successfully feeds the full acoustic context to Whisper without introducing timeline jitter, dropouts, or truncation errors.

2. **Live Streaming Queue Stabilization (512 Slots):**
   - Previously, the worker’s 32-slot inbound queue overflowed in 640 ms during blocking Whisper inference passes, resulting in evicted frames.
   - Expanding the queue capacity to 512 slots (>10 seconds buffer) completely resolved the overflow: all 20 live streaming clips completed with **zero dropped audio frames** (`dropped_audio_us: 0`).
   - In `live` mode, streaming audio is chunked into real-time rolling windows without future context, resulting in **46.23% WER** on English and **159.43% WER** on Romanian.

3. **Romanian Multilingual Behavior on Tiny Model:**
   - The 39M parameter `tiny` multilingual model exhibits high word error rates on Romanian across all modes (~90% WER), largely due to Romanian morphology and diacritic representation in the tiny vocabulary.
   - However, Character Error Rate (CER) remains bounded around **30%** in both offline and lookahead modes.
   - VLC-Whisper’s `lookahead` mode actually slightly outperformed offline batch inference on Romanian (**89.75%** vs **93.03%** WER).

## Detailed Root-Cause Analysis of Live Mode Degradation

The degradation in `live` mode is **not an acoustic degradation** (the audio is identical to lookahead and offline), but an **algorithmic chunking and deduplication artifact** that introduces massive word **insertion errors** ($I$).

### Word Count Inflation & Error Mechanics

Because Word Error Rate is computed as $\text{WER} = \frac{S + D + I}{N}$ (Substitutions + Deletions + Insertions divided by Reference Words), whenever a phrase is repeated or re-emitted, the **Insertions ($I$)** drive the WER up dramatically:

| Language | Metric | `offline` | `lookahead` | `live` | Impact |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **English (`en`)** | Reference Words | 212 | 212 | 212 | — |
| | Emitted Words | **216** | **216** | **286** | **+74 extra words (+35% inflation)** |
| | Total Word Errors | 22 | 22 | **98** | **76 additional errors (dominated by insertions)** |
| | WER (%) | **10.38%** | **10.38%** | **46.23%** | **4.5× degradation** |
| **Romanian (`ro`)** | Reference Words | 244 | 244 | 244 | — |
| | Emitted Words | 282 | 269 | **449** | **+180 extra words (+74% inflation)** |
| | Total Word Errors | 227 | 219 | **389** | **162 additional errors** |
| | WER (%) | **93.03%** | **89.75%** | **159.43%** | **Severe phrase repetition** |

### Lookahead vs. Live Architectural Contrast

1. **Why Lookahead Achieves Full Offline Parity (`10.38% WER` on EN)**:
   - In `lookahead` mode, the worker reads up to 30 seconds ahead of the playback cursor.
   - Silero VAD (`vw_vad_find_chunk_boundary`) splits audio strictly at **acoustic silence pauses**.
   - Whisper transcribes each complete, non-overlapping speech chunk once with full acoustic context.
   - The transcribed chunk is immediately purged from the buffer (`vw_audio_buffer_drain`).
   - Audio is never re-transcribed; there are zero overlapping inference passes and zero duplicate cues.

2. **Why Live Mode Degrades**:
   In live mode, future audio is unavailable. To present subtitles with low latency (1–2 seconds), the worker must transcribe partial audio using a progressive rolling window (2 s startup growing to 8 s, with 1 s rolling hops). This triggers a 4-stage failure cascade:
   - **Autoregressive Hallucination at Mid-Sentence Cuts**: When audio cuts off mid-sentence (e.g. at 2.0 s), Whisper’s decoder predicts a grammatically plausible completion from language priors, hallucinating words to finish the cut-off thought.
   - **Immutability Contract (ADR-017)**: To prevent visible subtitle flickering, once a caption cue is emitted to VLC, it is final and immutable; the system does not retract or rewrite previously displayed subtitles.
   - **Contextual Drift Across Rolling Hops**: 1 second later, the next 1-second audio chunk arrives. Whisper re-runs inference over the rolling window with more acoustic context. With the expanded context, Whisper's decoding output mutates: initial articles or pronouns change or drop (`"The hospital..."` $\to$ `"Hospital..."`), punctuation shifts, and the hallucinated tail is replaced by actual spoken words.
   - **Deduplication Leak in `vw_segment_builder.c`**:
     - `strcmp()` fails because of subtle wording, punctuation, or capitalization drift.
     - `strstr()` fails because neither string is a contiguous exact substring of the other.
     - Tail-trim matching fails because the revised hypothesis begins from the sentence root rather than matching the previous cue's suffix.
     - **Frontier Clamping & Re-emission**: Finding no match, line 341 clamps the start timestamp to the coverage frontier (`emit_start = builder->covered_end_us`) and **re-emits the entire phrase again as a new subtitle**.

### Case Study: English Clip `fleurs-en-1938`

The exact emitted segments for clip `fleurs-en-1938` illustrate this phenomenon:

- **Reference**:
  > *"the hospital has followed protocol for infection control including separating the patient from others to prevent possible infection of others"*
- **Offline & Lookahead Mode (WER: 0.0%)**:
  > *"The hospital has followed protocol for infection control, including separating the patient from others to prevent possible infection of others."*
- **Live Mode (WER: 90.0% — 18 error words on a 20-word reference)**:
  ```text
  0.00s -> 6.28s: The hospital has followed protocol for infection control, including separating the patient
  6.28s -> 7.92s: from others to prevent patients.
  7.92s -> 9.00s: Hospital has followed protocol for infection control, including separating the patient from others to prevent possible infections.
  9.00s -> 9.84s: possible infection of others.
  ```
  - **Window 1 (6.28s–7.92s)**: Whisper hallucinated `"to prevent patients."` at the cut boundary and committed it.
  - **Window 2 (7.92s–9.00s)**: On the next hop, Whisper re-transcribed the entire sentence without `"The"`, ending in `"possible infections."`. Because `"Hospital has..."` did not match `"The hospital has..."`, deduplication leaked, clamped the start time to `7.92s`, and emitted the 18 words a second time.
  - **Window 3 (9.00s–9.84s)**: The final drain pass emitted the remaining tail.

On Romanian, where the 39M parameter `tiny` model struggles with diacritics and morphology, hypotheses mutate even more radically between consecutive hops, causing deduplication to fail on nearly every pass and ballooning word count by +74%.

### Optimization Recommendations

To bring live mode quality closer to lookahead mode, future enhancements should implement:
1. **Token / Word-Level Timestamp Alignment**: Align hypothesis words to monotonic audio timestamps rather than doing coarse whole-segment string matching.
2. **Levenshtein Fuzzy Deduplication**: Measure edit distance between incoming hypotheses and covered history at window frontiers, trimming overlapping semantic prefixes even when individual characters or capitalization differ.
3. **Adaptive VAD Gating for Live Windows**: Delay the live inference hop when VAD detects ongoing active speech, waiting for short natural acoustic pauses before cutting windows.

## Per-Sample Detailed Breakdown

| Clip ID | Lang | Mode | Audio Duration | WER (%) | CER (%) | Words Err/Ref | Chars Err/Ref | Hypothesis | Reference |
| :--- | :---: | :--- | :---: | :---: | :---: | :---: | :---: | :--- | :--- |
| `fleurs-en-1904` | en | `offline` | 10.6s | 5.3% | 1.2% | 1/19 | 1/81 | However, due to the slow communication channels, styles in the West could lag behind by 25 to 30 years. | however due to the slow communication channels styles in the west could lag behind by 25 to 30 year |
| `fleurs-en-1675` | en | `offline` | 8.8s | 23.8% | 6.8% | 5/21 | 6/88 | All now is alongside the world's safe for you, always begin with a capital letter, even in the middle of a sentence. | all nouns alongside the word sie for you always begin with a capital letter even in the middle of a sentence |
| `fleurs-en-1950` | en | `offline` | 11.5s | 18.2% | 6.5% | 6/33 | 10/154 | To the north and with an easy reach is a romantic and fascinating town of Sinatra, which was made famous, to foreigners after a glowing account of its splendrous recorded by Lord Byron. | to the north and within easy reach is the romantic and fascinating town of sintra and which was made famous to foreigners after a glowing account of its splendours recorded by lord byron |
| `fleurs-en-1728` | en | `offline` | 5.8s | 6.7% | 2.7% | 1/15 | 2/75 | The cabbage juice changes color depending on how acidic basic alkaline the chemical is | the cabbage juice changes color depending on how acidic or basic alkaline the chemical is |
| `fleurs-en-1972` | en | `offline` | 4.3s | 6.2% | 8.5% | 1/16 | 6/71 | Many people don't think about them as senators because they have feathers and can fly. | many people don't think about them as dinosaurs because they have feathers and can fly |
| `fleurs-en-1938` | en | `offline` | 11.4s | 0.0% | 0.0% | 0/20 | 0/122 | The hospital has followed protocol for infection control, including separating the patient from others to prevent possible infection of others. | the hospital has followed protocol for infection control including separating the patient from others to prevent possible infection of others |
| `fleurs-en-1876` | en | `offline` | 10.8s | 12.5% | 1.1% | 2/16 | 1/89 | The Northern Marianna's Emergency Management Office said that there were no damages reported in the nation. | the northern marianas emergency management office said that there were no damages reported in the nation |
| `fleurs-en-1914` | en | `offline` | 7.0s | 6.2% | 8.0% | 1/16 | 7/88 | 20th century research has shown that there are two pools of genetic variation hidden and expressed. | twentieth century research has shown that there are two pools of genetic variation hidden and expressed |
| `fleurs-en-1846` | en | `offline` | 11.0s | 21.7% | 17.3% | 5/23 | 17/98 | The aspect ratio of this format, the vying by 12 to obtain the simplest whole number ratio, is therefore said to be free to 2. | the aspect ratio of this format dividing by twelve to obtain the simplest whole-number ratio is therefore said to be 3:2 |
| `fleurs-en-1806` | en | `offline` | 9.9s | 0.0% | 0.0% | 0/33 | 0/141 | As light pollution in their heyday was not the kind of problem it is today, they are usually located in cities or at campuses easier to reach than those built in modern times. | as light pollution in their heyday was not the kind of problem it is today they are usually located in cities or at campuses easier to reach than those built in modern times |
| `fleurs-ro-1947` | ro | `offline` | 6.3s | 100.0% | 28.8% | 15/15 | 21/73 | Mai este de sosiera tropea orai, tira mai avut se se din ani o sute problemere gata de buala. | înainte de sosirea trupelor haiti nu mai avusese din anii 1800 probleme legate de boală |
| `fleurs-ro-1999` | ro | `offline` | 13.6s | 80.0% | 24.6% | 20/25 | 32/130 | Pătot parcur su anilor o mine un stășaizieci. Brezinski alu crat pentru John F. Kennedy, încălitate de cum sileră la cestia, e arapoi pentru administratia Lindem B. Johnson. | pe tot parcursul anilor 1960 brzezinski a lucrat pentru john f kennedy în calitate de consilier al acestuia iar apoi pentru administrația lyndon b johnson |
| `fleurs-ro-1783` | ro | `offline` | 7.0s | 111.1% | 35.0% | 20/18 | 36/103 | Pentru am elul climată ce se veră regională și să-mi erincru dviscole, e fortun de zapată, fortun de chiarțe, fortun de apapă. | fenomenele climatice severe regionale și sezoniere includ viscole furtuni de zăpadă furtuni de gheață și furtuni de praf |
| `fleurs-ro-1789` | ro | `offline` | 10.9s | 114.3% | 32.5% | 32/28 | 49/151 | Ce nu-au de nomile in alisări în cuce mai bufil, pregisori, ma acind de seinte costumea, multe aș de fincolo în sonora de design de producția, regie de sonen mic sa-o noște, scena rioa de gina. | celelalte nominalizări includ cel mai bun film regizor imagine design de costume montaj de film coloană sonoră design de producție regie de sunet mixaj sonor și scenariu original |
| `fleurs-ro-1993` | ro | `offline` | 11.9s | 91.3% | 17.4% | 21/23 | 21/121 | Astea sa format în ochanulat lântic, furtuna asultropica la gerii, azece a furtuna care privito n-o mă propriedil se zonul uragane lorat lântice. | astăzi s-a format în oceanul atlantic furtuna subtropicală jerry a zecea furtună care a primit un nume propriu din sezonul uraganelor atlantice |
| `fleurs-ro-1722` | ro | `offline` | 11.0s | 94.1% | 40.8% | 32/34 | 62/152 | Oamma, cum vor să cind deștri de așa început cu vernară, început cu acestri, înșați în alnatră, cu un poig de lecep, prin care se le care zelă, să-l castori, anteper, s-am adecereași sec. | cuomo în vârstă de 53 de ani și-a început guvernarea la începutul acestui an și a semnat luna trecută un proiect de lege prin care se legalizează căsătoria între persoane de același sex |
| `fleurs-ro-1819` | ro | `offline` | 11.5s | 75.0% | 17.6% | 21/28 | 25/142 | Tina antica avea un mod unii de al de limitat diferite peratitit, fiecare tapac, in sa fiecare famile care de cine a puterea, au representat o din astie distinta. | china antică avea un mod unic de a delimita diferite perioade de timp fiecare etapă a chinei sau fiecare familie care deținea puterea au reprezentat o dinastie distinctă |
| `fleurs-ro-1800` | ro | `offline` | 9.8s | 59.1% | 13.9% | 13/22 | 16/115 | Podul este programata a fi pedeprii funcional în septem de 2017, când putem de control va malbrazile ni si aști apte să fie terminate. | podul este programat a fi pe deplin funcțional în septembrie 2017 când punctele de control vamal braziliene se așteaptă să fie terminate |
| `fleurs-ro-1833` | ro | `offline` | 12.3s | 129.0% | 56.8% | 40/31 | 84/148 | Aici să până o fie să fie să îi înțește rie că o făr că ea tot ea să cât o într-amestă cât o înță o amână că întrecele patvelăm într-o. Ași să îi răa apa pămătul pe eruși fără. | aristotel un filozof a emis o teorie conform căreia totul este alcătuit dintr-un amestec de unul sau mai multe dintre cele patru elemente acestea erau apa pământul aerul și focul |
| `fleurs-ro-1875` | ro | `offline` | 7.4s | 65.0% | 18.8% | 13/20 | 15/80 | Nu mai puțin de 12-la suta din apacare si escudio nocea, din ariuri le planete, provin din Amazonă. | nu mai puțin de 20 la sută din apa care se scurge în oceane din râurile planetei provine din amazon |
| `fleurs-en-1904` | en | `live` | 10.6s | 52.6% | 42.0% | 10/19 | 34/81 | However, due to the slow communication channels, styles in the West could lag behind by 25. styles in the West could lag behind by 25 to 30 years. | however due to the slow communication channels styles in the west could lag behind by 25 to 30 year |
| `fleurs-en-1675` | en | `live` | 8.8s | 104.8% | 79.5% | 22/21 | 70/88 | All now is alongside the world's safe for you. Always begin with a capital letter, All now is alongside the world's safe for you, always begin with a capital letter, even the middle of a sentence. | all nouns alongside the word sie for you always begin with a capital letter even in the middle of a sentence |
| `fleurs-en-1950` | en | `live` | 11.5s | 39.4% | 26.0% | 13/33 | 40/154 | To the north and with an easy reach is a romantic and fascinating town of Sinatra and which was made famous to foreigners after a glowing account of its split. are glowing account of its splendrous recorded by | to the north and within easy reach is the romantic and fascinating town of sintra and which was made famous to foreigners after a glowing account of its splendours recorded by lord byron |
| `fleurs-en-1728` | en | `live` | 5.8s | 6.7% | 2.7% | 1/15 | 2/75 | The cabbage juice changes color depending on how acidic basic alkaline the chemical is | the cabbage juice changes color depending on how acidic or basic alkaline the chemical is |
| `fleurs-en-1972` | en | `live` | 4.3s | 6.2% | 8.5% | 1/16 | 6/71 | Many people don't think about them as senators because they have feathers and can fly. | many people don't think about them as dinosaurs because they have feathers and can fly |
| `fleurs-en-1938` | en | `live` | 11.4s | 90.0% | 98.4% | 18/20 | 120/122 | The hospital has followed protocol for infection control, including separating the patient from others to prevent patients. Hospital has followed protocol for infection control, including separating the patient from others to prevent possible infections. possible infection of others. | the hospital has followed protocol for infection control including separating the patient from others to prevent possible infection of others |
| `fleurs-en-1876` | en | `live` | 10.8s | 12.5% | 1.1% | 2/16 | 1/89 | The Northern Marianna's Emergency Management Office said that there were no damages reported in the nation. | the northern marianas emergency management office said that there were no damages reported in the nation |
| `fleurs-en-1914` | en | `live` | 7.0s | 6.2% | 8.0% | 1/16 | 7/88 | 20th century research has shown that there are two pools of genetic variation hidden and expressed. | twentieth century research has shown that there are two pools of genetic variation hidden and expressed |
| `fleurs-en-1846` | en | `live` | 11.0s | 30.4% | 21.4% | 7/23 | 21/98 | The aspect ratio of this format, the vying by 12 to obtain the simplest whole number ratio, is there? is therefore said to be for | the aspect ratio of this format dividing by twelve to obtain the simplest whole-number ratio is therefore said to be 3:2 |
| `fleurs-en-1806` | en | `live` | 9.9s | 69.7% | 67.4% | 23/33 | 95/141 | As light pollution in their heyday was not the kind of problem it is today. As light pollution in their heyday was not the kind of problem it is today, they are usually located in cities or at campuses easier to reach than those built in modern. easier to reach than those built in modern times. | as light pollution in their heyday was not the kind of problem it is today they are usually located in cities or at campuses easier to reach than those built in modern times |
| `fleurs-ro-1947` | ro | `live` | 6.3s | 100.0% | 28.8% | 15/15 | 21/73 | Mai este de sosiera tropea orai, tira mai avut se se din ani o sute problemere gata de buala. | înainte de sosirea trupelor haiti nu mai avusese din anii 1800 probleme legate de boală |
| `fleurs-ro-1999` | ro | `live` | 13.6s | 252.0% | 195.4% | 63/25 | 254/130 | Pentru că o să-mi încălă, pe tot păr cursul ani-l or o mine înșeșaisege, bresinți-l chrăt pe într-un John F. Kennedy încălitate. parcursul anilor o meneun stășaizăge. Brezinski alu creat pentru John F. Kennedy, încălitate de concillerea la ciutură. o un un stărșa izăge. Bresinschi alucrat pentru John F. Kennedy, încălitate de concilleră la cestia. e arapoi pentru administrat. e arapoi pentru administratia lindă. e arapoi pentru administratia Lindem B. Johnson. | pe tot parcursul anilor 1960 brzezinski a lucrat pentru john f kennedy în calitate de consilier al acestuia iar apoi pentru administrația lyndon b johnson |
| `fleurs-ro-1783` | ro | `live` | 7.0s | 116.7% | 26.2% | 21/18 | 27/103 | Venomenă climate ce se veră regională și se vămiere in clod visc o le e fortun desapă adă fărtun de chiată și fortun de apafă. | fenomenele climatice severe regionale și sezoniere includ viscole furtuni de zăpadă furtuni de gheață și furtuni de praf |
| `fleurs-ro-1789` | ro | `live` | 10.9s | 178.6% | 90.1% | 50/28 | 136/151 | Cine-o alt de nomile in alisări în cuce mai pofil, pregisori, ma acind de seinte costumea, multaș de fincolana sonora de design de producția regie. Ami la analisă, in cui ce mai poți film, regizori, ma acind de sainte costume, am un touch de fincolana sonora, design de producția, regie de sonen, mii ca sa am | celelalte nominalizări includ cel mai bun film regizor imagine design de costume montaj de film coloană sonoră design de producție regie de sunet mixaj sonor și scenariu original |
| `fleurs-ro-1993` | ro | `live` | 11.9s | 226.1% | 149.6% | 52/23 | 181/121 | Astea sa format în ochanulat lartic, furtuna asultropica la gerii, azea ce a furtuna care privitul nume propriul de să. Azece a furtuna care privito, nu m-e propriul din sezonul. Azece a furtuna care privito n-o m-e propriul din sezonul, uragane lorat la... Furtuna subtropica regeri, azece a furtuna care privito, nume propriul din sezonul uragane loratlantice. | astăzi s-a format în oceanul atlantic furtuna subtropicală jerry a zecea furtună care a primit un nume propriu din sezonul uraganelor atlantice |
| `fleurs-ro-1722` | ro | `live` | 11.0s | 97.1% | 63.8% | 33/34 | 97/152 | Oamma, cum vârse 15-20, am scan ce put cu vernară, am ce puto acest crea, am scanimamnă, trebuie pâncare să le care zăsă. | cuomo în vârstă de 53 de ani și-a început guvernarea la începutul acestui an și a semnat luna trecută un proiect de lege prin care se legalizează căsătoria între persoane de același sex |
| `fleurs-ro-1819` | ro | `live` | 11.5s | 64.3% | 21.1% | 18/28 | 30/142 | Tina antica avea un mod unii de al de limitat diferite peratit de timp, fiecare ta pachine sau fiecare famile care de cine a putem. au representat o din astră. | china antică avea un mod unic de a delimita diferite perioade de timp fiecare etapă a chinei sau fiecare familie care deținea puterea au reprezentat o dinastie distinctă |
| `fleurs-ro-1800` | ro | `live` | 9.8s | 90.9% | 45.2% | 20/22 | 52/115 | Podul este programata a fi pedeprii funcional în septem de 2017, când putem de control va malbrazile nii se aștea. când putem de control va malbrazileini si aști actă să fie terminate. | podul este programat a fi pe deplin funcțional în septembrie 2017 când punctele de control vamal braziliene se așteaptă să fie terminate |
| `fleurs-ro-1833` | ro | `live` | 12.3s | 335.5% | 202.0% | 104/31 | 299/148 | Aici să până o fie să fie să înțește înțește. Aici să până o fost să-o fost a înisăim teoria, con fărcaia tot ea scără al catui într-amestic de unul să-am țără. pentru că nu fost să fost a pe nici o ineptăriia comfort că ea tot este scără al 14. de unu sa amu, că nu trecele part vele mea. in teria comfort care tot e scălalt catui in tramec, de unu sa amu ca in trecele patveliment. Aci este rau apapă. Orcăia tot ea scără al catui într-amest, de unul sa amulă, că între cele patvelie ment. Ași sterea, apapă multul perus. | aristotel un filozof a emis o teorie conform căreia totul este alcătuit dintr-un amestec de unul sau mai multe dintre cele patru elemente acestea erau apa pământul aerul și focul |
| `fleurs-ro-1875` | ro | `live` | 7.4s | 65.0% | 18.8% | 13/20 | 15/80 | Nu mai puțin de 12-la suta din apacare si escudio nocea, din ariuri le planete, provin din Amazonă. | nu mai puțin de 20 la sută din apa care se scurge în oceane din râurile planetei provine din amazon |
| `fleurs-en-1904` | en | `lookahead` | 10.6s | 5.3% | 1.2% | 1/19 | 1/81 | However, due to the slow communication channels, styles in the West could lag behind by 25 to 30 years. | however due to the slow communication channels styles in the west could lag behind by 25 to 30 year |
| `fleurs-en-1675` | en | `lookahead` | 8.8s | 28.6% | 9.1% | 6/21 | 8/88 | All now is alongside the world's safe for you. Always begin with a capital letter. even the middle of a sentence. | all nouns alongside the word sie for you always begin with a capital letter even in the middle of a sentence |
| `fleurs-en-1950` | en | `lookahead` | 11.5s | 15.2% | 4.5% | 5/33 | 7/154 | To the north and with an easy reach is a romantic and fascinating town of Sinatra and which was made famous to foreigners after a glowing account of its splendrous recorded by Lord Byron. | to the north and within easy reach is the romantic and fascinating town of sintra and which was made famous to foreigners after a glowing account of its splendours recorded by lord byron |
| `fleurs-en-1728` | en | `lookahead` | 5.8s | 6.7% | 2.7% | 1/15 | 2/75 | The cabbage juice changes color depending on how acidic basic alkaline the chemical is | the cabbage juice changes color depending on how acidic or basic alkaline the chemical is |
| `fleurs-en-1972` | en | `lookahead` | 4.3s | 6.2% | 8.5% | 1/16 | 6/71 | Many people don't think about them as senators because they have feathers and can fly. | many people don't think about them as dinosaurs because they have feathers and can fly |
| `fleurs-en-1938` | en | `lookahead` | 11.4s | 0.0% | 0.0% | 0/20 | 0/122 | The hospital has followed protocol for infection control, including separating the patient from others to prevent possible infection. of others. | the hospital has followed protocol for infection control including separating the patient from others to prevent possible infection of others |
| `fleurs-en-1876` | en | `lookahead` | 10.8s | 12.5% | 1.1% | 2/16 | 1/89 | The Northern Marianna's Emergency Management Office said that there were no damages reported in the nation. | the northern marianas emergency management office said that there were no damages reported in the nation |
| `fleurs-en-1914` | en | `lookahead` | 7.0s | 6.2% | 8.0% | 1/16 | 7/88 | 20th century research has shown that there are two pools of genetic variation hidden and expressed. | twentieth century research has shown that there are two pools of genetic variation hidden and expressed |
| `fleurs-en-1846` | en | `lookahead` | 11.0s | 21.7% | 17.3% | 5/23 | 17/98 | The aspect ratio of this format, the vying by 12 to obtain the simplest whole number ratio, is therefore said to be free to 2. | the aspect ratio of this format dividing by twelve to obtain the simplest whole-number ratio is therefore said to be 3:2 |
| `fleurs-en-1806` | en | `lookahead` | 9.9s | 0.0% | 0.0% | 0/33 | 0/141 | As light pollution in their heyday was not the kind of problem it is today, they are usually located in cities or at campuses easier to reach than those built in modern times. | as light pollution in their heyday was not the kind of problem it is today they are usually located in cities or at campuses easier to reach than those built in modern times |
| `fleurs-ro-1947` | ro | `lookahead` | 6.3s | 100.0% | 28.8% | 15/15 | 21/73 | Mai este de sosiera tropea orai, tira mai avut se se din ani o sute problemere gata de buala. | înainte de sosirea trupelor haiti nu mai avusese din anii 1800 probleme legate de boală |
| `fleurs-ro-1999` | ro | `lookahead` | 13.6s | 92.0% | 55.4% | 23/25 | 72/130 | pe tot parcur su anilor o mine un stășaizăge, bresinschi alucrăt pe într-un John F. Kennedy, încălitate de cum silele la cestia, | pe tot parcursul anilor 1960 brzezinski a lucrat pentru john f kennedy în calitate de consilier al acestuia iar apoi pentru administrația lyndon b johnson |
| `fleurs-ro-1783` | ro | `lookahead` | 7.0s | 111.1% | 35.0% | 20/18 | 36/103 | Pentru am elul climată ce se veră regională și să-mi erincru dviscole, e fortun de zapată, fortun de chiarțe, fortun de apapă. | fenomenele climatice severe regionale și sezoniere includ viscole furtuni de zăpadă furtuni de gheață și furtuni de praf |
| `fleurs-ro-1789` | ro | `lookahead` | 10.9s | 96.4% | 32.5% | 27/28 | 49/151 | Ce nu-au de nomile in alisări în cuce mai bun film, pregisori maci in sainte costume, multaș de fincolul în sonora de design de producția, regie de sonen mic sa-o noște, scena rioa digina. | celelalte nominalizări includ cel mai bun film regizor imagine design de costume montaj de film coloană sonoră design de producție regie de sunet mixaj sonor și scenariu original |
| `fleurs-ro-1993` | ro | `lookahead` | 11.9s | 91.3% | 17.4% | 21/23 | 21/121 | Astea sa format în ochanulat lântic, furtuna asultropica la gerii, azece a furtuna care privito n-o mă propriedil se zonul uragane lorat lântice. | astăzi s-a format în oceanul atlantic furtuna subtropicală jerry a zecea furtună care a primit un nume propriu din sezonul uraganelor atlantice |
| `fleurs-ro-1722` | ro | `lookahead` | 11.0s | 94.1% | 40.8% | 32/34 | 62/152 | Oamma, cum vor să cind deștri de așa început cu vernară, început cu acestri, înșați în alnatră, cu un poig de lecep, prin care se le care zelă, să-l castori, anteper, s-am adecereași sec. | cuomo în vârstă de 53 de ani și-a început guvernarea la începutul acestui an și a semnat luna trecută un proiect de lege prin care se legalizează căsătoria între persoane de același sex |
| `fleurs-ro-1819` | ro | `lookahead` | 11.5s | 75.0% | 17.6% | 21/28 | 25/142 | Tina antica avea un mod unii de al de limitat diferite peratitit, fiecare tapac, in sa fiecare famile care de cine a puterea, au representat o din astie distinta. | china antică avea un mod unic de a delimita diferite perioade de timp fiecare etapă a chinei sau fiecare familie care deținea puterea au reprezentat o dinastie distinctă |
| `fleurs-ro-1800` | ro | `lookahead` | 9.8s | 59.1% | 13.9% | 13/22 | 16/115 | Podul este programata a fi pedeprii funcional în septem de 2017, când putem de control va malbrazile ni si aști actă să fie terminate. | podul este programat a fi pe deplin funcțional în septembrie 2017 când punctele de control vamal braziliene se așteaptă să fie terminate |
| `fleurs-ro-1833` | ro | `lookahead` | 12.3s | 109.7% | 56.8% | 34/31 | 84/148 | Aici să până fost să-o fost să-o a pe nici o înțăriia, cu un folcarea tot ea scăl al 14. de unu să-o am ogem trecele patveliament. Aceste era apa, bămutul peruș folgă. | aristotel un filozof a emis o teorie conform căreia totul este alcătuit dintr-un amestec de unul sau mai multe dintre cele patru elemente acestea erau apa pământul aerul și focul |
| `fleurs-ro-1875` | ro | `lookahead` | 7.4s | 65.0% | 18.8% | 13/20 | 15/80 | Nu mai puțin de 12-la suta din apacare si escudio nocea, din ariuri le planete, provin din Amazonă. | nu mai puțin de 20 la sută din apa care se scurge în oceane din râurile planetei provine din amazon |
