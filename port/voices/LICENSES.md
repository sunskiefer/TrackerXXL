# Licences of the TrackerXXL voice library

The library as a whole is a derivative work of the **LXR02 Open-Source software** and is governed by its licence
(`licenses/LXR02-Open-Source.txt`, reproduced in full below): **it may not be sold or used in a commercial product or
activity, and every redistribution of a modified build must carry the complete source of everything in the binary.**
Parts that came from elsewhere keep their own, more permissive licences, listed here. Nothing in this directory is
under the GPL (the LXR 0.6 / Erica Synths firmware is not included; the engine is the 0.5.10 LXRXXL on LXR02Open).

Every vendored file keeps the copyright notice it had upstream. Texts are copied, not paraphrased, into `licenses/`.

| Part | Where | Licence | Notice / text |
|---|---|---|---|
| LXR02 Open-Source drum engine and hardware-independent DSP (Copyright 2026 Brendan Clarke; after Sonic Potions' LXR) | `third_party/lxr02open/` | LXR02 Open-Source licence (non-commercial, complete source) | `licenses/LXR02-Open-Source.txt`, `third_party/lxr02open/LICENSE` |
| LXRXXL 0.5.10 glue, FX unit, kit data (Copyright 2026 L'Cronx) | `lxr/` | the same LXR02 licence (it is a derivative work) | `lxr/LICENSE` |
| LXRXXL ZYNTH synth voices and chain (Copyright 2026 L'Cronx) | `zynth/` | the same LXR02 licence | `zynth/LICENSE` |
| LXRXXL BAZZLINE delay, gate/duck and bass bank (Copyright 2026 L'Cronx) | `bazz/` | the same LXR02 licence | `bazz/LICENSE` |
| TrackerXXL voice library glue: `trackerxxl_voices.*`, `tv_*.c`, `tv_internal.h`, `tools/`, `tests/` | top level | the same LXR02 licence, as it links the above | this file |
| Mutable Instruments Braids, Plaits (Copyright Emilie Gillet; years as in each file header) | `third_party/mutable/braids`, `plaits` | MIT | `licenses/stmlib-MIT.txt` (the MIT text Emilie Gillet publishes, same wording in each file header), `third_party/mutable/LICENSE` |
| Mutable Instruments stmlib (Copyright 2012 Emilie Gillet) | `third_party/mutable/stmlib` | MIT | `licenses/stmlib-MIT.txt`, `third_party/mutable/stmlib/LICENSE` |
| DX7 algorithm table in `zynth/src/eng_fm.c`, from music-synthesizer-for-android (`fm_core.cc`, Copyright 2012 Google Inc.) | `zynth/src/eng_fm.c` | Apache License 2.0 | `licenses/Apache-2.0.txt` |

Notes.

* `third_party/mutable/stmlib/LICENSE` is the file stmlib ships. It also mentions code in `third_party/STM` (STMicroelectronics)
  and `programming/serial` (GPL, Ivan A-R) of the upstream repository. **Neither directory is vendored here**; only
  `stmlib/dsp/*` and `stmlib/utils/*` files the build needs are present. The sentences are kept because the file is
  copied unchanged.
* Mutable Instruments files were modified for LXRXXL ZYNTH (44.1 kHz in `plaits/dsp/dsp.h`; wave map in
  `wavetable_engine.cc`); the changes are marked in the files and listed in `VENDORED.md`. MIT permits this; the notices stay.
* The Apache 2.0 text is the Debian `common-licenses` copy. msfa ships no NOTICE file for the portion used.
* Compatibility flag. The LXR02 licence forbids sale and commercial use. The TrackerXXL tree this directory sits in has
  its own `LICENSE.polyend` (MIT plus a clone restriction). Linking this library into a TrackerXXL binary makes that
  binary subject to the LXR02 terms (non-commercial, complete source). This was not reviewed by a lawyer; decide before
  distributing a combined binary.

## Text 1 -- LXR02 Open-Source licence (governs the library)

     *  Created on: 17.05.2026
     * ------------------------------------------------------------------------------------------------------------------------
     *  Copyright 2026 Brendan Clarke
     *  brendanpaulclarke@gmail.com
     *  https://www.brendanclarke.com
     * ------------------------------------------------------------------------------------------------------------------------
     *  This file is part of the LXR02 Open-Source software.
     * ------------------------------------------------------------------------------------------------------------------------
     *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
     *  provided that the following conditions are met:
     *
     *       - The code may not be sold, nor may it be used in a commercial product or activity.
     *
     *       - Redistributions that are modified from the original source must include the complete
     *         source code, including the source code for all components used by a binary built
     *         from the modified sources. However, as a special exception, the source code distributed
     *         need not include anything that is normally distributed (in either source or binary form)
     *         with the major components (compiler, kernel, and so on) of the operating system on which
     *         the executable runs, unless that component itself accompanies the executable.
     *
     *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
     *         following disclaimer in the documentation and/or other materials provided with the distribution.
     * ------------------------------------------------------------------------------------------------------------------------
     *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
     *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
     *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
     *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
     *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
     *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
     *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
     * ------------------------------------------------------------------------------------------------------------------------
     */

## Text 2 -- MIT licence (Mutable Instruments Braids, Plaits, stmlib)

    Except when noted otherwise, all code is copyright Emilie Gillet and is
    released under the MIT License with the following notice:
    
    Copyright 2012 Emilie Gillet.
    
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.


## Text 3 -- Apache License 2.0 (DX7 algorithm table)

The complete text is in `licenses/Apache-2.0.txt` (not duplicated here). The portion used is the algorithm table in
`zynth/src/eng_fm.c`, whose header names its origin and licence.
