# Stemmerizer — Third-Party Notices

Stemmerizer ("the Software") includes the following third-party components.
The license texts below are reproduced verbatim from upstream and apply to
their respective components only. Nothing in this NOTICE expands or limits
the rights granted by the Stemmerizer EULA over the Software as a whole.

For a deeper analysis of why each license is compatible with closed-source
commercial distribution, see `legal/LICENSE-AUDIT.md`.

---

## JUCE 8

The Software is built with the JUCE 8 framework
(https://github.com/juce-framework/JUCE), licensed by us under the JUCE 8
Commercial EULA (Starter tier).

> JUCE is © Raw Material Software Limited. JUCE is a trademark of Raw
> Material Software Limited.

The full JUCE 8 license text governing our use is available at
https://juce.com/legal/juce-8-licence/.

---

## demucs.cpp

The Software performs stem separation using `demucs.cpp`
(https://github.com/sevagh/demucs.cpp).

```
MIT License

Copyright (c) 2024 Sevag Hanssian

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
```

---

## Demucs (model architecture and pretrained weights)

```
MIT License

Copyright (c) Meta Platforms, Inc. and affiliates.

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
```

The pretrained Demucs v4 weights (`htdemucs`, `htdemucs_ft`, `htdemucs_6s`)
shipped with the Software are released by Meta under the same MIT terms.

---

## Eigen 3

Eigen is licensed under the Mozilla Public License 2.0 (MPL 2.0). Full text:
https://www.mozilla.org/en-US/MPL/2.0/. The Software links Eigen header-only
modules without modification.

A small number of Eigen components (not used by the Software) are LGPL.
The Software is configured to enable only MPL-2.0 modules; see
`CMakeLists.txt` for the EIGEN_ build flags.

---

## Steinberg VST 3 SDK

The Software ships in VST 3 plug-in format under the Steinberg VST 3
Plug-In SDK Licensing Agreement (Proprietary Licensing Option).

> "VST" is a trademark of Steinberg Media Technologies GmbH, registered in
> Europe and other countries.

---

## dr_libs (dr_wav, dr_flac, dr_mp3)

Public Domain (Unlicense) / MIT-0 dual-licensed by David Reid.
https://github.com/mackron/dr_libs

---

## Inter (UI font)

```
Copyright 2016 The Inter Project Authors (https://github.com/rsms/inter)

This Font Software is licensed under the SIL Open Font License, Version 1.1.
This license is copied below, and is also available with a FAQ at:
https://openfontlicense.org
```
(Full OFL 1.1 text shipped in `resources/fonts/Inter-OFL.txt`.)

---

## JetBrains Mono (UI mono font)

```
Copyright 2020 The JetBrains Mono Project Authors
(https://github.com/JetBrains/JetBrainsMono)

This Font Software is licensed under the SIL Open Font License, Version 1.1.
```
(Full OFL 1.1 text shipped in `resources/fonts/JetBrainsMono-OFL.txt`.)

---

## OpenMP runtime (vcomp140.dll)

Distributed as part of the Microsoft Visual C++ Redistributable under
Microsoft's Redistributable Code license terms.
