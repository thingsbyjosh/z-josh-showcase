import i18n from '../i18n';

// Demo-owned mail copy for the per-race report. Registered at runtime (the app bundle stays untouched).
// Keys live in the default 'translation' namespace, next to the engine's ms_* keys. Locales without an
// entry fall back to English.
const EN: Record<string, string> = {
  demo_mail_race_subj: 'Race report: {track}',
  demo_mail_race_win_v0: 'P1 at {track}, {player}. Nobody got near you today. Take the points, the team is already planning the next one.',
  demo_mail_race_win_v1: 'A clean win at {track}. The car was right and so were you. Keep the same routine for the next round.',
  demo_mail_race_win_v2: 'First across the line at {track}. The paddock noticed. Enjoy it for an evening, then back to work.',
  demo_mail_race_podium_v0: 'P{pos} at {track}, {player}. A podium is a podium: the team is pleased and the points count.',
  demo_mail_race_podium_v1: 'You brought it home on the podium at {track} (P{pos}). One more push next time and the top step is yours.',
  demo_mail_race_podium_v2: 'P{pos} at {track}. Solid, quick and tidy. That is the kind of weekend that builds a season.',
  demo_mail_race_points_v0: 'P{pos} at {track}, {player}. Points in the bag. Not the headline, but this is how a championship is built.',
  demo_mail_race_points_v1: 'You finished P{pos} at {track} and scored. The gap to the podium is small; the data from this race will help.',
  demo_mail_race_points_v2: 'A points finish at {track} (P{pos}). Steady work. We keep pushing for more.',
  demo_mail_race_back_v0: 'P{pos} at {track}, {player}. No points this time. The pace was not there, but the car came home.',
  demo_mail_race_back_v1: 'A tough one at {track}: P{pos}. We will look at the setup together before the next round.',
  demo_mail_race_back_v2: 'P{pos} at {track}. Not what either of us wanted. Reset, and bring the same focus next time.',
  demo_mail_race_dnf_v0: 'You retired at {track} while running P{pos}, {player}. These things happen. Shake it off; the next round is what matters.',
  demo_mail_race_dnf_v1: 'No finish at {track} (out in P{pos}). The team will check what went wrong. Rest and come back sharp.',
  demo_mail_race_dnf_v2: 'A DNF at {track}, P{pos} when you stopped. It stings, but the season is long.',
  demo_mail_offer_subj: 'Offer from {team}',
  demo_mail_offer_body: '{team} watched your last results and wants you in their {car}: {salary} Cr per race and a {bonus} Cr signing bonus. Answer here or negotiate the terms in the Market.',
  demo_offer_accept: 'Accept',
  demo_offer_decline: 'Decline',
  demo_mail_offer_signed: 'Signed with {team}.',
  demo_mail_offer_declined: 'Offer declined.',
  demo_mail_signed_subj: 'Welcome to {team}',
  demo_mail_signed_body: 'The contract with {team} is signed: {salary} Cr per race, plus {bonus} Cr on signing. You drive the {car} from the next round.',
  demo_mail_counter_subj: '{team} came back with a counter-offer',
  demo_mail_counter_body: '{team} will not go that high, but they met you halfway: {salary} Cr per race in the {car}. Open the negotiation in the Market to take it or walk away.',
  demo_mail_agreed_subj: '{team} accepts your terms',
  demo_mail_agreed_body: '{team} agreed to {salary} Cr per race in the {car}. Open the negotiation in the Market and sign to seal it.',
  demo_mail_sponsor_subj: '{sponsor} is on board',
  demo_mail_sponsor_body: '{sponsor} backs your season, {player}. Their bonus pays on every podium, and the sponsor shop opens after each one: more partners, better multipliers.',
};
const ES: Record<string, string> = {
  demo_mail_race_subj: 'Informe de carrera: {track}',
  demo_mail_race_win_v0: 'P1 en {track}, {player}. Nadie se te acercó hoy. Llévate los puntos, el equipo ya piensa en la siguiente.',
  demo_mail_race_win_v1: 'Victoria limpia en {track}. El coche estaba bien y tú también. Mantén la misma rutina para la próxima.',
  demo_mail_race_win_v2: 'Primero en la meta en {track}. El paddock se ha dado cuenta. Disfrútalo una noche y a trabajar.',
  demo_mail_race_podium_v0: 'P{pos} en {track}, {player}. Un podio es un podio: el equipo está contento y los puntos cuentan.',
  demo_mail_race_podium_v1: 'Terminaste en el podio de {track} (P{pos}). Un empujón más y el escalón alto es tuyo.',
  demo_mail_race_podium_v2: 'P{pos} en {track}. Sólido, rápido y limpio. Así se construye una temporada.',
  demo_mail_race_points_v0: 'P{pos} en {track}, {player}. Puntos en el bolsillo. No es el titular, pero así se gana un campeonato.',
  demo_mail_race_points_v1: 'Acabaste P{pos} en {track} y puntuaste. La distancia al podio es pequeña; los datos de esta carrera ayudarán.',
  demo_mail_race_points_v2: 'Zona de puntos en {track} (P{pos}). Trabajo constante. Seguimos apretando.',
  demo_mail_race_back_v0: 'P{pos} en {track}, {player}. Sin puntos esta vez. Faltó ritmo, pero el coche volvió a casa.',
  demo_mail_race_back_v1: 'Carrera dura en {track}: P{pos}. Revisaremos juntos el reglaje antes de la próxima.',
  demo_mail_race_back_v2: 'P{pos} en {track}. No era lo que queríamos. Reinicia y trae el mismo foco la próxima vez.',
  demo_mail_race_dnf_v0: 'Abandonaste en {track} cuando ibas P{pos}, {player}. Pasa. Sacúdetelo; lo que importa es la siguiente.',
  demo_mail_race_dnf_v1: 'Sin meta en {track} (fuera en P{pos}). El equipo revisará qué falló. Descansa y vuelve afilado.',
  demo_mail_race_dnf_v2: 'Abandono en {track}, P{pos} cuando paraste. Duele, pero la temporada es larga.',
  demo_mail_offer_subj: 'Oferta de {team}',
  demo_mail_offer_body: '{team} ha seguido tus últimos resultados y te quiere en su {car}: {salary} Cr por carrera y {bonus} Cr de prima de firma. Responde aquí o negocia las condiciones en el Mercado.',
  demo_offer_accept: 'Aceptar',
  demo_offer_decline: 'Rechazar',
  demo_mail_offer_signed: 'Firmado con {team}.',
  demo_mail_offer_declined: 'Oferta rechazada.',
  demo_mail_signed_subj: 'Bienvenido a {team}',
  demo_mail_signed_body: 'El contrato con {team} está firmado: {salary} Cr por carrera, más {bonus} Cr al firmar. Conduces el {car} desde la próxima ronda.',
  demo_mail_counter_subj: '{team} responde con una contraoferta',
  demo_mail_counter_body: '{team} no llega a tanto, pero se queda a medio camino: {salary} Cr por carrera en el {car}. Abre la negociación en el Mercado para aceptarla o retirarte.',
  demo_mail_agreed_subj: '{team} acepta tus condiciones',
  demo_mail_agreed_body: '{team} acepta {salary} Cr por carrera en el {car}. Abre la negociación en el Mercado y firma para cerrarlo.',
  demo_mail_sponsor_subj: '{sponsor} se suma',
  demo_mail_sponsor_body: '{sponsor} respalda tu temporada, {player}. Su prima paga en cada podio, y la tienda de patrocinio se abre tras cada uno: más socios, mejores multiplicadores.',
};

let done = false;
/** Idempotent: safe to call any number of times. */
export function registerDemoI18n(): void {
  if (done) return;
  done = true;
  i18n.addResourceBundle('en', 'translation', EN, true, true);
  i18n.addResourceBundle('es', 'translation', ES, true, true);
}
