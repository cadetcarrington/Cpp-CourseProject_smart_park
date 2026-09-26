# Plate recognition examples

These four unmodified 720 x 1160 CCPD full-frame photographs are small integration examples, not a representative evaluation set. Their labels are decoded from the original CCPD filenames and are not predictions. Select two images per blue/green validation pool by the lowest path-hash suffix in the existing local manifests (blue: `ccpd_rec_smoke/val.txt`; green: `ccpd_rec/val_green.txt`).

| Example | Expected plate | Original CCPD filename | SHA-256 |
| --- | --- | --- | --- |
| `blue-arl321.jpg` | 皖ARL321 | `00241379310345-90_90-301&482_388&518-390&519_307&519_306&481_389&481-0_0_15_10_27_26_25-115-17.jpg` | `64fb2bdcf72900a0e3c422d30793a7f7b152197d2b8835f076c116883e3cbec7` |
| `blue-at260j.jpg` | 皖AT260J | `002344348659-90_84-429&369_530&406-525&405_425&398_428&364_528&371-0_0_17_26_30_24_8-105-11.jpg` | `ff78ab63a2bacf7c7f52e18ba682bf71546b67d1c96233c2cbb1ca21eafb039f` |
| `green-ad07180.jpg` | 皖AD07180 | `0312109375-93_258-223&449_529&552-529&552_234&527_223&449_526&467-0_0_3_24_31_25_32_24-86-29.jpg` | `80aaf0b0b7a3bf4baf451f93aac934e2052bc09d95bb3ffbc38b5db8a1b0b4fe` |
| `green-ad06151.jpg` | 皖AD06151 | `0240625-95_264-206&378_437&483-437&483_207&450_206&378_435&402-0_0_3_24_30_25_29_25-163-89.jpg` | `b3b5ae2fa050de69fb03eb8296ead8d284114732f00ee23ed2bb2e58f6852acc` |

Blue frames originate from CCPD2019 `ccpd_base`; its supplied MIT license and copyright notice are retained in [CCPD2019-LICENSE](CCPD2019-LICENSE). Please cite *Towards End-to-End License Plate Detection and Recognition: A Large Dataset and Baseline* when using CCPD2019. Green frames originate from CCPD2020 `ccpd_green/val`; the project owner confirmed redistribution permission for these selected images, but no upstream license text was available locally, so no specific license is asserted for them.

These are research-dataset images containing legible license plates, not anonymized or independently consent-verified photographs. Do not add privately collected camera imagery to this directory without a separate privacy and rights review.
