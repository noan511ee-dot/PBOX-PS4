<p align="center">
  <img src="PBOX-logo.jpg" alt="PBOX logo" width="180">
</p>

<h1 align="center">PBOX</h1>
<p align="center"><strong>Independent PS4 Cloud Gaming Client</strong></p>
<p align="center">A lightweight native dashboard and streaming client for homebrew-enabled PlayStation 4 systems.</p>

## Overview

PBOX is an independent PS4 cloud gaming client focused on a responsive native interface, reliable streaming, and practical controller support. It presents an Xbox-inspired Games dashboard with featured artwork, game details, a Play action, library browsing, console navigation, settings, and streaming diagnostics.

PBOX is not an emulator. It does not emulate Xbox or PlayStation hardware, copy games, bypass cloud-service authentication, or distribute copyrighted game content. The client requests a streaming session from a supported cloud service and forwards the resulting video, audio, and input data to the PS4.

The project is designed for a homebrew-enabled PS4 environment. A compatible PS4 firmware, a compatible homebrew environment such as GoldHEN, a supported cloud account, and a suitable streaming subscription may be required. Service availability, server region, account permissions, and network quality are outside the control of this repository.

نسخة **PBOX** الخاصة بجهاز **PlayStation 4** لبث ألعاب Xbox السحابية (xCloud)، بإعداد وتطوير وتخصيص **نواف العتيبي (Ravok)**، مبنية فوق الأساس مفتوح المصدر المرخّص تحت **GPL-3.0**.

## حقوق النسخة والتطوير
- **إعداد وتطوير وتخصيص النسخة:** نواف العتيبي (Ravok).
- **الأساس مفتوح المصدر:** مرخّص تحت رخصة **GNU General Public License v3.0 (GPL-3.0)** مع الإبقاء الكامل على ملف [`LICENSE`](LICENSE) وملف [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) وإشعارات المكونات والمكتبات التابعة (`mbedtls`, `libdatachannel`, `FFmpeg`, وغيرها).
- **الشفافية والمصدر:** بما أن المشروع موزع تحت رخصة **GPL-3.0**، فإن الكود المصدري متاح ومفتوح بنفس الرخصة.

## أبرز التخصيصات والاتجاه التطويري
- **تحسين مكتبة الألعاب المشتراة:** متابعة ومعالجة ملكية الألعاب المشتراة (`hasEntitlement` و`alternateProductIds`) في منطق المكتبة وخدمة `gssv` لضمان ظهور الألعاب المملوكة وتشغيلها بسلاسة.
- **تعريب وتجربة مستخدم عربية:** تطوير دعم اللغة العربية والواجهة المخصّصة لمستخدمي PS4.
- **بناء تلقائي لملف `.pkg`:** سير عمل GitHub Actions لبناء حزمة التثبيت الخاصة بـ PS4 وإخراجها مباشرة في Artifacts / Releases.
- **تكامل التطوير مع Manus:** هيكل منظم جاهز لإضافة التحسينات والواجهات والميزات الجديدة خطوة بخطوة.
