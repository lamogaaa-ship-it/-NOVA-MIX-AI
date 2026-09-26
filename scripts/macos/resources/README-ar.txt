NOVA MIX AI — طريقة التسطيب على الماك
======================================

1) افتح "Install NOVA MIX AI.pkg" (أو "1 - Install NOVA MIX AI.pkg").
   النسخة دي لسه مش موقّعة من Apple (مش Notarized). لو الماك قالك إن البرنامج
   "من مطوّر غير معروف":
     - اعمل كليك يمين (Control + كليك) على ملف الـ .pkg واختار "Open"، وبعدين "Open" تاني،
     - أو افتح System Settings ← Privacy & Security واضغط "Open Anyway".

2) سيب الـ 3 مكونات متعلّم عليهم (VST3 و Audio Unit و Standalone)، أو اختار من "Customize".
   أماكن التسطيب:
     VST3        /Library/Audio/Plug-Ins/VST3/NOVA MIX AI.vst3
     Audio Unit  /Library/Audio/Plug-Ins/Components/NOVA MIX AI.component
     التطبيق      /Applications/NOVA MIX AI.app

3) في FL Studio:
     Options ← Manage plugins ← اضغط "Find more plugins" (أو Find installed plugins)،
     وبعدين حط NOVA MIX AI على الـ Mixer insert بتاع الفوكال أو الماستر.
   في Logic Pro: هتلاقيه تحت Audio Units ← NOVA Audio.

4) الاستخدام:
     - شغّل التراك ودوس LISTEN — NOVA بيسمع الصوت الحقيقي ويحلّله.
     - اكتب طلبك بالعربي أو الإنجليزي في خانة الـ AI Assistant، مثلاً:
       "الصوت حاد شوية والـ S عالية، خليه أنعم من غير ما يبقى مكتوم"
     - استخدم A/B و Delta عشان تسمع الفرق (بنفس مستوى الصوت)، و Undo لو ماعجبكش.

الصوت (اختياري)
- ثبّت "NOVA Companion" (الـ installer التاني في الـ DMG الكامل) عشان تتكلم مع NOVA
  بالعربي أو الإنجليزي ويرد عليك بالصوت. بيشتغل في الخلفية على جهازك بس (127.0.0.1)
  وبيبدأ مع تشغيل الماك. أول مرة تدوس على زرار الميكروفون الماك هيطلب إذن الميكروفون،
  وموديل الكلام (حوالي 460 ميجا) بيتنزّل مرة واحدة.
- من الإعدادات (الترس) تقدر تختار "Voice language: العربية" عشان التعرّف يبقى أدق.
- من غير الـ Companion كل حاجة تانية شغالة، وزرار الكلام هيقول "Voice off".

المهندس الذكي
- المهندس الأوفلاين شغال من غير أي حساب أو إنترنت.
- المهندس الأونلاين (Claude) اختياري: من الإعدادات حط عنوان NOVA Cloud والتوكن،
  أو مفتاح Claude API لنسخ المطورين. الصوت نفسه عمره ما بيخرج من جهازك؛ اللي بيتبعت
  بس أرقام التحليل والكلام اللي كتبته، ولما تفعّل المهندس الأونلاين بس.

إزالة التسطيب
- امسح الـ 3 حاجات اللي فوق، واختياري: ~/Library/Application Support/NOVA MIX AI
