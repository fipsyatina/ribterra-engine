# Environment V1 — діагностичний спільний вхід освітлення й атмосфери

08.10.2026. Source implementation + [обмежений native GPU PASS](ENVIRONMENT_CHECK_2026_10_08_UA.md).
**E06/E10/E01–E17 OPEN.** Не host-authoritative календар/погода, не normal
startup, не остаточні художні криві, не географічна модель сонця.

`EngineEnvironmentSettingsV1`: version=1, hour [0,24), cloud01/haze01 [0,1],
atmosphere_enabled. Усі числа finite. Input exclusive з manual atmosphere;
requires water і renderer **WaterAtmosphereSlice**, навіть коли fog disabled.
GameView не може інспектувати обраний graph; неправильний renderer відхиляє
frame своїм контрактом. Normal/legacy/manual composition не переключається.

Environment **володіє** ambient, sun, moon і atmosphere. Manual sky inputs
не є входами цього режиму й перезаписуються; тільки external local-light
counts/rows зберігаються byte-exact. Authored windows додаються після цього
окремо; emission mask/scale/local profiles не тьмяніють разом із сонцем.

## Versioned художня крива

Solar phase = pi*(hour-6)/12. Day/night height = positive/negative sin phase;
abs(sin)<1e-6 задається нулем, щоб sin(pi) residue не активував shadow source.
Sun azimuth = 45° + 180°*(hour-6)/12; elevation20→65° за day height.
Sun intensity=.9*smoothstep(0,.2,dayHeight)*(1-.75*cloud).
Sun linear RGB: warm(.95,.55,.30) → high(1,.95,.82) за smoothstep(0,.5,height).
Нижче художнього 20° floor contribution плавно зникає, нескінченно довгих
active shadows немає. Directions normalized, toward-source, reserved W=0.

Moon opposite azimuth, elevation20→55° за nightHeight;
intensity=.18*smoothstep(0,.2,nightHeight)*(1-.85*cloud), coldRGB(.55,.68,1).
Це V1 протилежна добова крива, не lunar phases/астрономія.

Daylight=smoothstep(5,9,hour)*(1-smoothstep(16,21,hour)), той самий як у fog.
Ambient lerp night(.025,.04,.07) → day(.22,.24,.25)*(1-.45cloud).
Haze передається atmosphere adapter з **тією самою** hour; лише haze input
не змінює lighting/moon. Cloud і haze — незалежні явно задані diagnostic
числа, не інферований сезон/прогноз/client RNG. Shown seconds для water/fog
залишається єдиним переданим clock, не wall time; paused input повторюється.

## Потрібний обмежений доказ

Real GameView/ClientSession + native D3D12, shared HDR/shadow/reflection graph:

- Resolved manual equivalence, repeated frame, raw water classes і UI exact.
- Hour sequence0/6/9/12/15/18/21/3; cloud weakening та haze-only isolation.
- Sun9/15 і Moon21/3: caster-on minus caster-off masks, nonempty та relocation
  центроїдів на exact land carrier. Інший sky source має нульову інтенсивність.
- Midnight nextafter(24,0)→0 numeric tolerance1e-5, не byte-exact claim.
- Horizon5.9/6/6.1 і17.9/18/18.1: нуль джерел рівно6/18, successful render,
  same-hour sky-on/off delta: рівно6/18 exact, поруч positive. Ambient,
  local і emission у цій парі незмінні. Без твердження monotone total energy
  або exact shadow topology handoff.
- Incoming active local rows і appended authored rows exact; emission metadata
  unchanged; окреме native emission-only night delta тепле. Hue test має
  нульове інше освітлення й emission scale .125, щоб SDR clamp не інвертував
  поканальну різницю. Це не hue measurement насиченого повного night кадру.
- Invalid version/hour/cloud/haze/conflict/no-water rejected; published
  frame/picks/camera і наступний GPU result exact, не all-internal-cache proof.

Не приймати цей адаптер як повний E06 без normal integration, руху/відео,
усіх художніх категорій, authoritative time/weather і performance.
Нових shader variants, uniform ABI або attachments для цього кроку немає.
