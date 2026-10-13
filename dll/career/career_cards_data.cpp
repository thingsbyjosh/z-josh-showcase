// career_cards_data.cpp -- catalog v2 (100 cards, 5 rarities). 27 from v1 plus 73 from v2.
// A living-data pattern but WITHOUT ImGui: it's covered by the test harness (an integrity test covers all of it).
#include "career_cards.h"

namespace career {

std::vector<CardDef> CARD_DEFS = {
    // LOCAL (rarity 0)
    // 0.7.0 (): the tyres of a phase 1 car cost ~200 per race, so the -10 % did not exceed
    // 20 ₡. The discount stays and the brand pays a fixed 150 per race: it pays off in ~15 races.
    { "card_neumasport", "NeumaSport", "Gomas del barrio", "Neighbourhood rubber",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TYRES, 0.10,
      { 1, 5, 3000, 0.20, 0.0 }, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 150 } } },
    { "card_helicam", "HeliCam Live", "Tu carrera, en directo", "Your race, live",
      0, CARD_LIFE_USES, 3, "top8", CARD_HOOK_PRIZE_MULT, 2.0,
      {}, {}, "", "", "" },
    { "card_cafepaddock", "Caf\xC3\xA9 Paddock", "El espresso de boxes", "The pit-lane espresso",
      0, CARD_LIFE_RACES, 6, "clean", CARD_HOOK_INCOME_BONUS, 600,
      {}, {}, "", "", "" },
    { "card_talleraurora", "Taller Aurora", "Chapa, pintura y fe", "Bodywork and faith",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_ENTRY, 0.35,   // 0.7.0 (): was 0.15 (75 ₡ per race)
      {}, { 1, 800, 0 }, "", "", "" },
    { "card_radioboxes", "Radio Boxes", "La voz del paddock", "The paddock's voice",
      0, CARD_LIFE_RACES, 8, "passive", CARD_HOOK_XP_MULT, 1.10,
      {}, {}, "", "", "" },
    { "card_gruavelox", "Gr\xC3\xBA""a V\xC3\xA9lox", "Si no rompes, cobras", "Don't break it, get paid",
      0, CARD_LIFE_RACES, 5, "damagefree", CARD_HOOK_INCOME_BONUS, 900,
      {}, {}, "", "", "" },
    { "card_lubrisur", "LubriSur", "Aceites del sur", "Southern oils",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_FUEL, 0.10,   // 0.7.0 (): + fixed 150 (the -10 % was 12 ₡)
      { 1, 6, 2800, 0.18, 0.0 }, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 150 } } },

    // REGIONAL (rarity 1)
    { "card_volt", "Volt Energy", "Ench\xC3\xBA""fate al podio", "Plug into the podium",
      1, CARD_LIFE_RACES, 8, "podium", CARD_HOOK_INCOME_BONUS, 2500,
      {}, {}, "", "", "" },
    { "card_fueltec", "FuelTec Iberia", "Cada gota cuenta", "Every drop counts",
      1, CARD_LIFE_PERM, 0, "fueluse", CARD_HOOK_INCOME_BONUS, 1200,
      { 1, 6, 6000, 2000, 0.0 }, {}, "", "", "" },
    { "card_chicane", "Hostal La Chicane", "Cama, ducha y parrilla", "Bed, shower, grid",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.25,
      {}, {}, "", "", "" },
    { "card_tribuna", "La Tribuna Motor", "Titulares que pagan", "Headlines that pay",
      1, CARD_LIFE_RACES, 10, "win", CARD_HOOK_INCOME_BONUS, 3000,
      {}, { 2, 0, 3 }, "", "", "" },
    { "card_pistasegura", "PistaSegura", "Aseguradora de circuito", "Circuit insurance co.",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_RENT, 0.30,
      { 1, 5, 7000, 0.50, 0.0 }, {}, "", "", "" },
    { "card_voltasur", "VoltaSur Bater\xC3\xAD""as", "Energ\xC3\xAD""a sin incidentes", "Incident-free energy",
      1, CARD_LIFE_USES, 5, "clean", CARD_HOOK_PRIZE_MULT, 1.30,   // 0.7.0 (): x1.25 fell 185 short of paying for itself
      {}, {}, "", "", "" },

    // NATIONAL (rarity 2)
    { "card_transair", "TransAir Cargo", "Tu equipo, ya est\xC3\xA1 all\xC3\xAD", "Your kit is already there",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.60,   // 0.7.0 (): was 0.40; travel does not exceed 2,000
      {}, { 1, 1500, 0 }, "", "", "" },
    { "card_neumax", "NeuMax Pro", "Compuestos de f\xC3\xA1""brica", "Factory compounds",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TYRES, 0.25,   // 0.7.0 (): + fixed 700
      { 1, 7, 16000, 0.40, 0.0 }, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 700 } } },
    { "card_kronos", "Kronos Timing", "El crono es dinero", "The clock is money",
      2, CARD_LIFE_RACES, 12, "win", CARD_HOOK_PRIZE_MULT, 1.30,
      {}, {}, "", "", "" },
    { "card_vector", "Academia Vector", "Talento bajo presi\xC3\xB3n", "Talent under pressure",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_XP_MULT, 1.25,
      {}, {}, "", "", "" },
    { "card_petroendur", "PetroEndur", "Nacidos para durar", "Born to last",
      2, CARD_LIFE_PERM, 0, "endur", CARD_HOOK_INCOME_BONUS, 6000,
      {}, {}, "", "", "" },
    { "card_boxesco", "Boxes & Co", "Corredores de ofertas", "Deal brokers",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_SPECIAL, 0.0,
      {}, {}, "shop.free_reroll",
      "El primer reroll de cada tienda es gratis", "First reroll of every shop is free" },

    // INTERNATIONAL (rarity 3)
    { "card_meridian", "Agencia Meridian", "Representaci\xC3\xB3n global", "Global representation",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_SPECIAL, 0.0,
      {}, {}, "market.offer_quality",
      "Las ofertas del mercado llegan un escal\xC3\xB3n mejores", "Market offers arrive one step better" },
    { "card_gpglobal", "GP Global Media", "Derechos de imagen", "Image rights",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.15,
      { 1, 8, 40000, 1.25, 0.0 }, {}, "", "", "" },
    { "card_scout21", "Scout 21", "Ojeadores de patrocinio", "Sponsorship scouts",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_SPECIAL, 0.0,
      {}, {}, "shop.extra_offer",
      "La tienda muestra una 4\xC2\xAA carta", "The shop shows a 4th card" },
    { "card_titanfuel", "Titan Fuel Intl.", "Refinado para ganar", "Refined to win",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_FUEL, 0.35,   // 0.7.0 (): + fixed 2,000
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 2000 } } },
    { "card_vidalserra", "Vidal & Serra", "Bufete deportivo", "Motorsport law firm",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_SPECIAL, 0.0,
      {}, {}, "nego.leverage",
      "+10 de margen al negociar contratos", "+10 leverage in contract talks" },

    // ELITE (rarity 4)
    { "card_doble", "Doble Espacio", "Doble hueco en tu librea", "Twice the livery space",
      4, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_SPECIAL, 0.0,
      {}, {}, "shop.extra_buy",
      "M\xC3\xA1x. de compra por tienda: 2 cartas", "Max buys per shop: 2 cards" },
    { "card_apexcrown", "Corona Apex", "Solo firmamos campeones", "We only sign champions",
      4, CARD_LIFE_PERM, 0, "win", CARD_HOOK_PRIZE_MULT, 1.60,
      {}, { 1, 8000, 0 }, "", "", "" },
    { "card_stratos", "Stratos Air", "La flota del campeonato", "The championship fleet",
      4, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.60,   // 0.7.0 (): + fixed 4,500 (travel alone
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 4500 } } },     // saved 1,200 per race against 110,000)

    // ================= Catalog v2 -- 73 new cards: risk/reward duals, bets
    // usage-based, hard clauses and wild ones. The 27 above are KEPT byte-identical (tests).
    // Totals: local 30 / regional 25 / national 20 / international 15 / elite 10.

    // LOCAL v2 (rarity 0, +23)
    // standard
    { "card_lacurva", "Bar La Curva", "Men\xC3\xBA del piloto", "The driver's set menu",
      0, CARD_LIFE_RACES, 10, "passive", CARD_HOOK_INCOME_BONUS, 300,
      { 1, 6, 2500, 500, 0.0 }, {}, "", "", "" },
    { "card_fixio", "Fixio Tools", "La llave que no resbala", "The wrench that never slips",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_ENTRY, 0.30,   // 0.7.0 (): was 0.10 (50 ₡), N2 0.18 for 2,600
      { 1, 5, 1000, 0.45, 0.0 }, {}, "", "", "" },
    { "card_kartopia", "Kartopia", "Donde empezaste", "Where you started",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_XP_MULT, 1.08,
      { 1, 5, 2800, 1.15, 0.0 }, {}, "", "", "" },
    { "card_donpiston", "Don Pist\xC3\xB3n", "Recambios con palabra", "Spare parts, word of honour",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_RENT, 0.10,
      {}, {}, "", "", "" },
    { "card_matecorsa", "Mate Corsa", "Calma al volante", "Calm behind the wheel",
      0, CARD_LIFE_RACES, 8, "clean", CARD_HOOK_INCOME_BONUS, 500,
      {}, {}, "", "", "" },
    { "card_ecodrive", "EcoDrive School", "Conduce fino, cobra", "Drive smooth, get paid",
      0, CARD_LIFE_RACES, 8, "tyrewear", CARD_HOOK_INCOME_BONUS, 600,
      { 1, 6, 3200, 1000, 0.0 }, {}, "", "", "" },
    // risk/reward duals
    { "card_chispacola", "Chispa Cola", "Az\xC3\xBA""car y octanaje", "Sugar and octane",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.10,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -250 } } },
    { "card_tuercaloca", "La Tuerca Loca", "Baratas y casi nuevas", "Cheap and nearly new",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TYRES, 0.15,   // 0.7.0 (): + fixed 150
      {}, {}, "", "", "", { { CARD_HOOK_REP_MULT, 0.92 }, { CARD_HOOK_INCOME_BONUS, 150 } } },
    { "card_grafiti", "Grafiti Wraps", "Que te miren", "Make them stare",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.10,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -300 } } },
    { "card_curvados", "Curvados Craft", "Un brindis por cada podio", "A toast to every podium",
      0, CARD_LIFE_RACES, 8, "podium", CARD_HOOK_INCOME_BONUS, 800,
      {}, {}, "", "", "", { { CARD_HOOK_XP_MULT, 0.95 } } },
    { "card_buho", "Garaje B\xC3\xBAho", "Abrimos cuando cierran", "Open when they close",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_RENT, 0.20,
      {}, {}, "", "", "", { { CARD_HOOK_XP_MULT, 0.93 } } },
    { "card_piratatv", "PirataTV", "Emitimos sin permiso", "Broadcasting without a licence",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.12,
      {}, {}, "", "", "", { { CARD_HOOK_REP_MULT, 0.88 } } },
    { "card_ahorroil", "AhorrOil", "Aceite de outlet", "Outlet-grade oil",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_FUEL, 0.15,   // 0.7.0 (): + fixed 150
      {}, {}, "", "", "", { { CARD_HOOK_REP_MULT, 0.95 }, { CARD_HOOK_INCOME_BONUS, 150 } } },
    { "card_vecinos", "Pe\xC3\xB1""a Los Vecinos", "Tu grada de siempre", "Your home grandstand",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.12,
      { 1, 6, 3000, 1.18, 0.0 }, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -200 } } },
    { "card_relampago", "Rel\xC3\xA1mpago Courier", "Llega antes que t\xC3\xBA", "There before you are",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.15,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -150 } } },
    // usage-based bets
    { "card_fortunilla", "Kiosko Fortuna", "Doble o nada, chaval", "Double or nothing, kid",
      0, CARD_LIFE_USES, 2, "podium", CARD_HOOK_PRIZE_MULT, 2.2,
      {}, {}, "", "", "" },
    { "card_selfiegrid", "SelfieGrid", "Tu cara en todas partes", "Your face everywhere",
      0, CARD_LIFE_USES, 3, "top8", CARD_HOOK_INCOME_BONUS, 1200,
      {}, {}, "", "", "" },
    { "card_abuela", "Croquetas La Abuela", "Sin toques, con croquetas", "No contact, more croquettes",
      0, CARD_LIFE_USES, 4, "clean", CARD_HOOK_INCOME_BONUS, 800,
      {}, {}, "", "", "" },
    { "card_petardo", "Pirotecnia El Petardo", "Solo si hay traca", "Only if there's fireworks",
      0, CARD_LIFE_USES, 2, "win", CARD_HOOK_PRIZE_MULT, 2.5,
      {}, {}, "", "", "" },
    { "card_hipermiler", "Hipermiler", "La \xC3\xBAltima gota es gratis", "The last drop rides free",
      0, CARD_LIFE_USES, 3, "fueluse", CARD_HOOK_INCOME_BONUS, 900,
      {}, {}, "", "", "" },
    // hard clauses
    { "card_turbocred", "Cr\xC3\xA9""ditos Turbo", "Cobramos siempre", "We always collect",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_INCOME_BONUS, 700,
      {}, { 1, 2500, 0 }, "", "", "" },
    { "card_fenix", "Desguace F\xC3\xA9nix", "Piezas con pasado", "Parts with a past",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.15,
      {}, { 1, 2000, 0 }, "", "", "" },
    { "card_curvasur", "Curva Sur", "Contigo hasta el top-10", "With you down to P10",
      0, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.15,
      {}, { 2, 0, 2 }, "", "", "" },

    // REGIONAL v2 (rarity 1, +19)
    // standard
    { "card_orbita", "\xC3\x93rbita Telemetr\xC3\xAD""a", "Datos que ense\xC3\xB1""an", "Data that teaches",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_XP_MULT, 1.15,
      { 1, 6, 6500, 1.22, 0.0 }, {}, "", "", "" },
    { "card_gripline", "GripLine", "Agarre certificado", "Certified grip",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TYRES, 0.18,   // 0.7.0 (): + fixed 400
      { 1, 6, 7000, 0.28, 0.0 }, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 400 } } },
    { "card_horizonte", "Hotel Horizonte", "Duerme en la pole", "Sleep on pole",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.25,   // 0.7.0 (): was 0.20 / N2 0.30 (it stayed at 0)
      { 1, 6, 6000, 0.35, 0.0 }, {}, "", "", "" },
    { "card_vatioverde", "Vatio Verde", "Energ\xC3\xAD""a con conciencia", "Power with a conscience",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_FUEL, 0.20,   // 0.7.0 (): + fixed 400
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 400 } } },
    { "card_cronopost", "CronoPost", "Cada finde, un sobre", "An envelope every weekend",
      1, CARD_LIFE_RACES, 12, "passive", CARD_HOOK_INCOME_BONUS, 900,
      { 1, 6, 6800, 1400, 0.0 }, {}, "", "", "" },
    // risk/reward duals
    { "card_shokka", "Shokka!", "Sube el pico, baja el foco", "Spike up, focus down",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.18,
      {}, {}, "", "", "", { { CARD_HOOK_XP_MULT, 0.93 } } },
    { "card_lowfly", "LowFly Air", "Asiento 32F, garantizado", "Seat 32F, guaranteed",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.35,
      {}, {}, "", "", "", { { CARD_HOOK_XP_MULT, 0.92 } } },
    { "card_coinpit", "CoinPit", "Tu premio, tokenizado", "Your prize, tokenized",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.25,
      {}, {}, "", "", "", { { CARD_HOOK_REP_MULT, 0.80 } } },
    { "card_martillo", "Subastas El Martillo", "Tu gloria, al mejor postor", "Your glory to the highest bidder",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.20,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -700 } } },
    { "card_vintagepad", "Paddock Vintage", "Estilo antes que premio", "Style over prize money",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.18,
      {}, {}, "", "", "", { { CARD_HOOK_PRIZE_MULT, 0.90 } } },
    { "card_dynotune", "DynoTune", "El banco no miente", "The dyno never lies",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_XP_MULT, 1.20,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -500 } } },
    { "card_caravana", "Caravana Box", "Tu casa en el paddock", "Your home in the paddock",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_RENT, 0.40,   // 0.7.0 (): was 0.30 with a fee of 650 (it lost 50 per race)
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -300 } } },
    { "card_fisiopit", "FisioPit", "Cuello de piloto, no de oficina", "Racing neck, not desk neck",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.15,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -450 } } },
    // usage-based bets
    { "card_koibet", "KoiBet", "Apuesta por ti", "Bet on yourself",
      1, CARD_LIFE_USES, 3, "win", CARD_HOOK_PRIZE_MULT, 2.5,
      {}, {}, "", "", "" },
    { "card_dronpass", "DronPass", "Te seguimos desde arriba", "We follow from above",
      1, CARD_LIFE_USES, 4, "top8", CARD_HOOK_INCOME_BONUS, 2300,   // 0.7.0 (): 4 x 2,000 = its fair price
      {}, {}, "", "", "" },
    { "card_nocturnagear", "Nocturna Gear", "Aguanta y cobra", "Outlast and cash in",
      1, CARD_LIFE_USES, 2, "endur", CARD_HOOK_PRIZE_MULT, 1.8,
      {}, {}, "", "", "" },
    { "card_ceramik", "Ceramik Coats", "Ni un roce", "Not a scratch",
      1, CARD_LIFE_USES, 3, "damagefree", CARD_HOOK_INCOME_BONUS, 3000,   // 0.7.0 (): 3 x 1,700 = 5,100 against 8,000
      {}, {}, "", "", "" },
    // hard clauses
    { "card_taurox", "Taurox Energy", "Sin miedo o sin nada", "Fearless or penniless",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.30,
      { 1, 6, 7500, 1.5, 0.0 }, { 1, 4000, 0 }, "", "", "" },
    { "card_genaro", "Genaro Import-Export", "No le falles a Genaro", "Don't let Genaro down",
      1, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_INCOME_BONUS, 1800,
      {}, { 2, 0, 2 }, "", "", "" },

    // NATIONAL v2 (rarity 2, +14)
    // standard
    { "card_iberwatch", "IberWatch", "Cada podio, un reloj", "A watch for every podium",
      2, CARD_LIFE_RACES, 12, "podium", CARD_HOOK_INCOME_BONUS, 4000,
      { 1, 7, 16000, 5500, 0.0 }, {}, "", "", "" },
    { "card_bancoluz", "Banco Luz", "Tu aval en la parrilla", "Your guarantee on the grid",
      2, CARD_LIFE_RACES, 15, "passive", CARD_HOOK_INCOME_BONUS, 2500,
      { 1, 7, 15000, 3500, 0.0 }, {}, "", "", "" },
    { "card_clipracer", "ClipRacer Studio", "Tus highlights, virales", "Your highlights, viral",
      2, CARD_LIFE_PERM, 0, "top8", CARD_HOOK_INCOME_BONUS, 3200,
      {}, {}, "", "", "" },
    // risk/reward duals
    { "card_helvetica", "Helvetica Time", "La elegancia no cobra premio", "Elegance takes no prize",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.25,
      {}, {}, "", "", "", { { CARD_HOOK_PRIZE_MULT, 0.85 } } },
    { "card_maxtorque", "MaxTorque Oil", "Potencia por suscripci\xC3\xB3n", "Horsepower on subscription",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.30,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -1200 } } },
    { "card_jetset", "JetSet Racing Club", "Vuela como los grandes", "Fly like the big teams",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.75,   // 0.7.0 (): was 0.45 with a fee of 1,000,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -300 } } },     // more fee than saving: it lost on every race
    { "card_vandalo", "V\xC3\xA1ndalo Streetwear", "Icono antes que atleta", "Icon before athlete",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.30,
      {}, {}, "", "", "", { { CARD_HOOK_XP_MULT, 0.90 } } },
    { "card_datamine", "DataMine Analytics", "Sabemos por qu\xC3\xA9 pierdes", "We know why you lose",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_XP_MULT, 1.30,
      { 1, 7, 15000, 1.45, 0.0 }, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -900 } } },
    { "card_fortum", "Fortum Apuestas", "La banca te quiere", "The house loves you",
      2, CARD_LIFE_PERM, 0, "win", CARD_HOOK_PRIZE_MULT, 1.60,
      { 1, 7, 17000, 1.8, 0.0 }, {}, "", "", "", { { CARD_HOOK_REP_MULT, 0.90 } } },
    // usage-based bets
    { "card_titanio", "Grupo Titanio", "Construye tu leyenda", "Build your legend",
      2, CARD_LIFE_USES, 3, "podium", CARD_HOOK_PRIZE_MULT, 2.0,
      {}, {}, "", "", "" },
    { "card_marathon", "Marathon Fuels", "Repostamos leyendas", "We fuel legends",
      2, CARD_LIFE_USES, 2, "endur", CARD_HOOK_PRIZE_MULT, 2.2,
      {}, {}, "", "", "" },
    { "card_purasangre", "Purasangre Films", "La c\xC3\xA1mara quiere drama", "The camera wants drama",
      2, CARD_LIFE_USES, 2, "win", CARD_HOOK_INCOME_BONUS, 12500,   // 0.7.0 (): 2 x 9,000 against 22,000
      {}, {}, "", "", "" },
    // hard clauses
    { "card_kamikaze", "Kamikaze Brakes", "Frena o revienta", "Brake or break",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.40,
      {}, { 1, 8000, 0 }, "", "", "" },
    { "card_portada", "Grupo Portada", "Portada o nada", "Front page or nothing",
      2, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_INCOME_BONUS, 3000,
      {}, { 2, 0, 2 }, "", "", "" },

    // INTERNATIONAL v2 (rarity 3, +10)
    // standard
    { "card_zephyr", "Zephyr Airways", "Embarque prioritario", "Priority boarding",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TRAVEL, 0.50,   // 0.7.0 (): + fixed 1,800
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 1800 } } },
    { "card_auriga", "Auriga Composites", "Fibra de campeones", "Champion-grade fibre",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_ENTRY, 0.40,
      { 1, 8, 38000, 0.55, 0.0 }, {}, "", "", "" },
    // risk/reward duals
    { "card_goldcrest", "Goldcrest & Sons", "El prestigio no firma cheques", "Prestige signs no cheques",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.40,
      { 1, 8, 42000, 1.6, 0.0 }, {}, "", "", "", { { CARD_HOOK_PRIZE_MULT, 0.75 } } },
    { "card_hypercoin", "HyperCoin X", "Fortuna vol\xC3\xA1til", "Volatile fortune",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.50,
      {}, {}, "", "", "", { { CARD_HOOK_REP_MULT, 0.70 } } },
    { "card_monarch", "Monarch Hotels", "Suite con vistas a boxes", "A suite over the pit lane",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_RENT, 0.45,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -1500 } } },
    { "card_apexstream", "ApexStream+", "El documental eres t\xC3\xBA", "You are the documentary",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_INCOME_BONUS, 4500,
      {}, {}, "", "", "", { { CARD_HOOK_XP_MULT, 0.90 } } },
    // usage-based bets
    { "card_vegasgrand", "Vega$ Grand", "Todo al rojo", "All on red",
      3, CARD_LIFE_USES, 2, "win", CARD_HOOK_PRIZE_MULT, 2.8,
      {}, {}, "", "", "" },
    { "card_borealis", "Borealis Endurance", "Vestimos la madrugada", "Dressed for 4 a.m.",
      3, CARD_LIFE_USES, 3, "endur", CARD_HOOK_INCOME_BONUS, 20000,   // 0.7.0 (): 3 x 8,000 against 52,000
      {}, {}, "", "", "" },
    // hard clauses
    { "card_shogun", "Shogun Gearbox", "El honor no se aver\xC3\xAD""a", "Honour never breaks down",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.45,
      {}, { 1, 12000, 0 }, "", "", "" },
    { "card_imperialbk", "Imperial Bank", "El banco no espera", "The bank doesn't wait",
      3, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_INCOME_BONUS, 5000,
      {}, { 2, 0, 2 }, "", "", "" },

    // ELITE v2 (rarity 4, +7)
    // standard
    { "card_pantheon", "Pantheon Motors", "Ingenier\xC3\xAD""a de dinast\xC3\xAD""a", "Dynasty engineering",
      4, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_COST_TYRES, 0.40,   // 0.7.0 (): + fixed 4,200
      { 1, 8, 90000, 0.55, 0.0 }, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, 4200 } } },
    { "card_olympus", "Olympus Telemetry", "Ves lo que nadie ve", "See what nobody sees",
      4, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_XP_MULT, 1.40,
      { 1, 9, 95000, 1.55, 0.0 }, {}, "", "", "" },
    // risk/reward duals
    { "card_midas", "Midas Capital", "El toque de oro se alquila", "The golden touch, for rent",
      4, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.45,
      {}, {}, "", "", "", { { CARD_HOOK_INCOME_BONUS, -2000 } } },
    { "card_couture", "Couture Paddock", "La marca eres t\xC3\xBA", "You are the brand",
      4, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_REP_MULT, 1.50,
      {}, {}, "", "", "", { { CARD_HOOK_PRIZE_MULT, 0.70 } } },
    { "card_leviathan", "Leviathan Energy", "El gigante paga bien", "The giant pays well",
      4, CARD_LIFE_PERM, 0, "passive", CARD_HOOK_PRIZE_MULT, 1.60,
      {}, {}, "", "", "", { { CARD_HOOK_REP_MULT, 0.75 }, { CARD_HOOK_INCOME_BONUS, -1500 } } },
    // usage-based bet
    { "card_everest", "Everest Chrono", "La cima o nada", "The summit or nothing",
      4, CARD_LIFE_USES, 2, "win", CARD_HOOK_PRIZE_MULT, 3.0,
      {}, {}, "", "", "" },
    // hard clause
    { "card_ragnarok", "Ragnar\xC3\xB6k Oil", "El fin del mundo, patrocinado", "The apocalypse, sponsored",
      4, CARD_LIFE_PERM, 0, "endur", CARD_HOOK_PRIZE_MULT, 2.5,
      {}, { 1, 15000, 0 }, "", "", "" },
};

// 0.7.0 (): cards that are not bought for money. Their main effect is XP, REP or a special, and several
// charge for it (fee or trimmed prize). test_card_best_case_pays skips them; any other card that does not
// earn money in its best case breaks the harness. Reasons in ASCII: only the test reads them.
const std::vector<CardRoiNote> CARD_ROI_BY_DESIGN = {
    { "card_radioboxes", "+10% de XP durante 8 carreras; no paga dinero" },
    { "card_kartopia",   "+8% de XP; no paga dinero" },
    { "card_curvasur",   "+15% de REP a cambio de no salir del top-10; no paga dinero" },
    { "card_grafiti",    "vende dinero por REP: +10% de REP con cuota de 300 por carrera" },
    { "card_vecinos",    "vende dinero por REP: +12% de REP con cuota de 200 por carrera" },
    { "card_orbita",     "+15% de XP; no paga dinero" },
    { "card_vintagepad", "vende premio por REP: +18% de REP y premio x0.90" },
    { "card_dynotune",   "vende dinero por XP: +20% de XP con cuota de 500 por carrera" },
    { "card_fisiopit",   "vende dinero por REP: +15% de REP con cuota de 450 por carrera" },
    { "card_vector",     "+25% de XP; no paga dinero" },
    { "card_boxesco",    "especial de tienda: el primer reroll de cada tienda sale gratis" },
    { "card_helvetica",  "vende premio por REP: +25% de REP y premio x0.85" },
    { "card_vandalo",    "+30% de REP a cambio de -10% de XP; no paga dinero" },
    { "card_datamine",   "vende dinero por XP: +30% de XP con cuota de 900 por carrera" },
    { "card_meridian",   "especial de mercado: las ofertas llegan un nivel por encima" },
    { "card_scout21",    "especial de tienda: cuarta carta en cada tienda" },
    { "card_vidalserra", "especial de contratos: +10 de margen al negociar" },
    { "card_goldcrest",  "vende premio por REP: +40% de REP y premio x0.75" },
    { "card_doble",      "especial de tienda: dos compras por tienda" },
    { "card_olympus",    "+40% de XP; no paga dinero" },
    { "card_couture",    "vende premio por REP: +50% de REP y premio x0.70" },
};

} // namespace career
