#include "strings.h"
#include "strings_locale.h"
#include <cstring>

static int g_lang = 0;   // 0=en (default), 1=es
void SetLang(int l){ g_lang = (l==1)?1:0; }
int  GetLang(){ return g_lang; }

struct KV{ const char* k; const char* en; const char* es; };
static KV TBL[] = {
  // item 7: loading cover (Career Engine brand) — useful rotating tips while the mod sets up the race.
  {"loading_configuring","Setting up your race\xE2\x80\xA6","Preparando tu carrera\xE2\x80\xA6"},
  {"tip_dont_touch","Don't change the settings we've configured for this race.","No cambies los ajustes que hemos configurado para esta carrera."},
  // VISUAL post-load notice in Custom Race (doesn't block clicks): the config is already applied.
  {"start_hint_kicker","ALL SET","TODO LISTO"},
  // (08/19): the notice used to say only "don't touch anything", and anyone WATCHING the screen sees values that haven't
  // updated yet, assumes it failed, and touches them anyway -- the opposite of what we want. So it's told
  // that's normal until the race finishes loading.
  {"start_hint","Your race is configured. Some of it will not show here until the race loads, so do not worry if something looks wrong: leave it and hit Start.","Tu carrera ya est\xC3\xA1 configurada. Algunos valores no se ven aqu\xC3\xAD hasta que cargue la carrera, as\xC3\xAD que no te preocupes si algo parece mal: d\xC3\xA9jalo y dale a Empezar."},
  {"tip_online_restart","Racing online next? Close the app and restart the game first.","\xC2\xBFVas a correr online? Cierra la app y reinicia el juego antes."},
  {"tip_sim_to_end","Finished early? Press 'Simulate to end' to settle the race.","\xC2\xBFTerminaste antes? Pulsa 'Simular hasta el final' para cerrar la carrera."},
  {"tip_wait_line","For a clean result, wait until everyone crosses the line.","Para un resultado limpio, espera a que todos crucen la meta."},
  {"tip_edit_profile","You can edit your driver profile any time in Settings.","Puedes editar tu perfil de piloto cuando quieras en Ajustes."},
  {"tip_change_difficulty","You can change the difficulty in Settings.","Puedes cambiar la dificultad en Ajustes."},
  {"tip_dda_slider","Adaptive difficulty has a fine-tune slider: stay adaptive but nudge it your way.","La dificultad Adaptativa tiene un ajuste fino: sigue en adaptativa pero mu\xC3\xA9vela a tu gusto."},
  // +(08/26): three more tips — the save is crash-proof against game failures, the Settings screen, report bugs.
  {"tip_no_midrace_save","Your career only saves once the race settles, so a mid-race game hiccup can't corrupt it.","Tu carrera solo se guarda al cerrarse la carrera: un fallo del juego a mitad no puede estropearla."},
  {"tip_more_settings","There are plenty more options waiting in the Settings tab.","Recuerda que tienes muchas m\xC3\xA1s opciones en la secci\xC3\xB3n Ajustes."},
  {"tip_report_bugs","Spotted a bug? Even a tiny one counts: report it on our Discord or the other official channels.","\xC2\xBFHas visto un bug? Aunque sea m\xC3\xADnimo, no te lo guardes: rep\xC3\xB3rtalo en nuestro Discord o en los canales oficiales."},
  {"tip_ctrl_f","Looking for something? Ctrl+F opens a find bar on any screen of the app.","\xC2\xBF" "Buscas algo? Ctrl+F abre un buscador en cualquier pantalla de la app."},
  // cover screen summary — field labels + special values. Weather is ALWAYS shown as text.
  {"cov_race","RACE","CARRERA"},
  {"cov_laps","laps","vueltas"},
  {"cov_rivals","RIVALS","RIVALES"},
  {"cov_difficulty","AI DIFFICULTY","DIFICULTAD IA"},
  {"cov_weather","WEATHER","CLIMA"},
  {"cov_hour","START TIME","HORA"},
  {"cov_start","START","SALIDA"},
  {"cov_start_standing","Standing","Parada"},
  {"cov_start_rolling","Rolling","Lanzada"},
  {"cov_pit","PIT RULES","BOXES"},
  {"cov_pit_req","Mandatory stop","Parada obligatoria"},
  {"cov_pit_free","No mandatory stop","Sin parada obligatoria"},
  {"cov_sessions","PRACTICE / QUALI","PR\xC3\x81" "CTICA / QUALI"},   // split escape: \x81 followed by 'C' (hex) would be greedy
  {"cov_inherited","Your setup","Tu configuraci\xC3\xB3n"},
  // PRACTICE never actually turns on (only its duration gets set), so the cover screen can't announce it
  // as if it would happen: "60' if active". "Off" = we turned it off ourselves (for measurement).
  {"cov_sess_off","Off","Desactivada"},
  {"cov_sess_ifon"," if enabled"," si est\xC3\xA1 activa"},
  // groups the verification notice can name (car/track don't appear on the cover, but they can still fail).
  {"cov_car","CAR","COCHE"},
  {"cov_track","TRACK","CIRCUITO"},
  // variant of the post-load notice for when verification finds values that did NOT land. Telling the player
  // "all set, don't touch anything" with the wrong car or time is exactly what caused races to run misconfigured.
  {"start_hint_warn_kicker","CHECK THE SETUP","REVISA LA CONFIG"},
  {"start_hint_warn","We could not apply everything. Check this before hitting Start:","No hemos podido aplicarlo todo. Revisa esto antes de darle a Empezar:"},
  // 0.5.4: AMS2's Shared Memory is turned off. Without it the mod can't see the race start or finish, so it
  // can't settle the race: telling the player "all set" would be a lie. This notice outranks the other two because it's the only one
  // that makes the whole race pointless. It deliberately shares its text with the app's banner (shm.warnTitle/warnBody):
  // one single wording for the notice shown by both the game and the app.
  // Split into kicker/message/detail so it lands in the SAME 3-line layout the other variant already uses
  // without touching the pill's layout: the detail is the literal instruction, in monospace.
  {"start_hint_shm_kicker","SHARED MEMORY OFF","SHARED MEMORY APAGADA"},
  {"start_hint_shm","This race will NOT be scored: the mod cannot see it start or finish.","Esta carrera NO se va a registrar: el mod no la ve empezar ni terminar."},
  {"start_hint_shm_fix","AMS2 > Options > System > Shared Memory = \"Project CARS 2\", then restart the game","AMS2 > Opciones > Sistema > Shared Memory = \"Project CARS 2\", y reinicia el juego"},
  // Canonical catalog of AMS2 weather states (reverse-engineered 07/17; same vocabulary as the app's events.wx_*). 9/10/11 unmapped.
  {"wxname_0","Clear","Despejado"},
  {"wxname_1","Partly cloudy","Parcialmente nublado"},
  {"wxname_2","Medium cloud","Medianamente nublado"},
  {"wxname_3","Mostly cloudy","Mayormente nublado"},
  {"wxname_4","Overcast","Nublado"},
  {"wxname_5","Drizzle","Llovizna"},
  {"wxname_6","Rain","Lluvia"},
  {"wxname_7","Storm","Tormenta"},
  {"wxname_8","Thunderstorm","Tormenta el\xC3\xA9" "ctrica"},   // split escape: \xA9 + 'c' (hex) would be greedy
  {"wxname_12","Hazy","Neblinoso"},
  {"wxname_13","Misty with rain","Neblina con lluvia"},
  {"wxname_14","Dense fog","Niebla densa"},
  {"wxname_15","Dense fog with rain","Niebla densa y lluvia"},
  {"wxname_16","Mist","Neblina"},
  {"wxname_17","Random","Aleatorio"},
  {"champ_master_title","REAL SERIES \xC2\xB7 CALENDAR","SERIES REALES \xC2\xB7 CALENDARIO"},
  {"cal_next","NEXT","PR\xC3\x93XIMO"},
  {"champs_tab","CHAMPIONSHIPS","CAMPEONATOS"},
  {"inbox_tab","INBOX","CORREOS"},
  {"sponsors_tab","SPONSORS","SPONSORS"},
  {"memories_tab","MEMORIES","RECUERDOS"},
  {"champ_stand_tab","TIMES","TIEMPOS"},
  // The Standings tab is reused as a race-TIMES log (best lap / total time / per-race result).
  {"times_title","Race Times","Tiempos de carrera"},
  {"times_sub","Your best lap and race time per race","Tu mejor vuelta y tiempo total por carrera"},
  {"times_empty","No races yet. Run a race to log your times.","A\xC3\xBAn no hay carreras. Corre una para registrar tus tiempos."},
  {"times_date","DATE","FECHA"},
  {"times_track","TRACK","CIRCUITO"},
  {"times_class","CLASS","CLASE"},
  {"times_result","RESULT","RESULTADO"},
  {"times_best","BEST LAP","MEJOR VUELTA"},
  {"times_total","RACE TIME","TIEMPO"},
  // full per-race grid + drill-down
  {"times_open","Open a race for the full grid","Abre una carrera para ver la parrilla completa"},
  {"times_back","\xE2\x86\x90 BACK","\xE2\x86\x90 VOLVER"},
  {"times_pos","POS","POS"},
  {"times_driver","DRIVER","PILOTO"},
  {"times_laps","LAPS","VUELTAS"},
  {"times_gap","GAP","DIF."},
  {"times_grid_empty","No per-driver data for this race.","Sin datos por piloto en esta carrera."},
  {"times_note","Race time = sum of laps \xC2\xB7 gap to the winner (laps down shown as +N L)","Tiempo = suma de vueltas \xC2\xB7 diferencia al ganador (vueltas perdidas como +N V)"},
  {"times_lap_abbr","L","V"},
  // the ImGui overlay's Sponsors screen was retired to a placeholder (the system now lives in the app, via cards).
  {"sp_legacy_moved","Sponsors are managed in the Career Engine app","Sponsors se gestiona en la app Career Engine"},
  // Sponsors UI port (of data_living.jsx's LIVING_STR sponsor keys) — keys with no reference left in the overlay after
  // the sponsors screen was gutted; they're KEPT: the external app's i18n dump still uses them.
  {"sp_title","Sponsors","Sponsors"},
  {"sp_sub","Capture and leverage sponsors","Capta y aprovecha patrocinadores"},
  {"sp_my","My sponsors","Mis sponsors"},
  {"sp_available","Available","Disponibles"},
  {"sp_coverage","Coverage","Cobertura"},
  {"sp_flavor","Brand","Imagen"},
  {"sp_objective","Objective","Objetivo"},
  {"sp_remaining","remaining","restante"},
  {"sp_rounds","rounds","rondas"},
  {"sp_noeffect","No gameplay effect","Sin efecto de juego"},
  {"sp_active","Active","Activo"},
  {"sp_atrisk","At risk","En riesgo"},
  {"sp_expiring","Expiring","Por caducar"},
  {"sp_sign","Sign","Firmar"},
  {"sp_signed","Signed","Firmado"},
  {"sp_unmet","Unmet","Sin cumplir"},
  {"sp_cant_afford","Can't afford","Sin fondos"},
  {"sp_no_slot","No slot","Sin plaza"},
  {"sp_no_fit","Off-discipline","Otra disciplina"},
  {"sp_contact_req","Requirement to contact","Requisito para contactar"},
  {"sp_active_n","active","activos"},
  {"sp_capture_n","to capture","captables"},
  // Results MONEY line for the thematic fixed payout (distinct from the retainer "sponsor").
  {"sp_payout","Sponsor payout","Pago de patrocinador"},
  // sponsor mail payload keys (bilingual-by-TR; family=2). Announce on sign, payout on objective.
  {"sp_mail_announce","Welcome aboard. We're proud to back your campaign. Hit your objective and the bonus is yours.",
                      "Bienvenido a bordo: nos enorgullece respaldar tu campa\xC3\xB1""a. Cumple tu objetivo y el bono es tuyo."},
  {"sp_mail_payout","Objective met. Your sponsor bonus has been paid into your account. Outstanding work.",
                    "Objetivo cumplido: hemos abonado tu bono de patrocinador en tu cuenta. Trabajo sobresaliente."},
  // Card mail payload keys (bilingual-by-TR; family=2, shared sponsor voice).
  // Sponsor cards, extended by the living-mail-system pass: sponsor cards
  // rewritten with the full commercial voice (subj+v0..v2; the bare key = v0, a fallback for old saves).
  {"sp_mail_card_buy_subj","Welcome aboard","Bienvenido a bordo"},
  {"sp_mail_card_buy_v0","Dear {player}: the agreement is signed and our brand now rides with your programme. We back drivers, not promises. Make the space we bought worth watching. {brand}, sponsorships","Estimado {player}: el acuerdo queda firmado y nuestra marca ya viaja con tu programa. Apostamos por pilotos, no por promesas. Haz que el espacio que compramos merezca la pena verse. {brand}, patrocinios"},
  {"sp_mail_card_buy_v1","{player}, the ink is dry. Our name is on your programme starting today, and our people will be watching the timing screens with interest. Give them something to clip for the newsletter. {brand}","{player}, la tinta ya est\xC3\xA1 seca. Nuestro nombre acompa\xC3\xB1""a tu programa desde hoy, y nuestra gente mirar\xC3\xA1 los tiempos con inter\xC3\xA9s. Dales algo que recortar para el bolet\xC3\xADn. {brand}"},
  {"sp_mail_card_buy_v2","Agreement live as of today, {player}. Brands remember two things: results and how a driver carries them. We expect to enjoy both. {brand}, partnerships","Acuerdo en vigor desde hoy, {player}. Las marcas recordamos dos cosas: los resultados y c\xC3\xB3mo los lleva un piloto. Esperamos disfrutar de ambas. {brand}, patrocinios"},
  {"sp_mail_card_buy","Dear {player}: the agreement is signed and our brand now rides with your programme. We back drivers, not promises. Make the space we bought worth watching. {brand}, sponsorships","Estimado {player}: el acuerdo queda firmado y nuestra marca ya viaja con tu programa. Apostamos por pilotos, no por promesas. Haz que el espacio que compramos merezca la pena verse. {brand}, patrocinios"},
  {"sp_mail_card_clause_subj","Regarding our agreement","Sobre nuestro acuerdo"},
  {"sp_mail_card_clause_v0","Dear {player}: under the current agreement, the latest result triggers the penalty clause. The amount has been settled against your account. We trust the coming rounds will return to the standard that earned our backing. {brand}, sponsorships","Estimado {player}: conforme al acuerdo vigente, el \xC3\xBAltimo resultado activa la cl\xC3\xA1usula de penalizaci\xC3\xB3n. El importe se ha liquidado contra tu cuenta. Confiamos en que las pr\xC3\xB3ximas citas recuperen el nivel que motiv\xC3\xB3 nuestra apuesta. {brand}, patrocinios"},
  {"sp_mail_card_clause_v1","{player}: our agreement includes terms for weekends like this one, and they have been applied. No hard feelings. Contracts keep both sides honest. Show us the version of you we signed. {brand}","{player}: nuestro acuerdo incluye condiciones para fines de semana como este, y se han aplicado. Sin rencores. Los contratos nos mantienen honestos a ambos. Ens\xC3\xA9\xC3\xB1""anos la versi\xC3\xB3n de ti que firmamos. {brand}"},
  {"sp_mail_card_clause_v2","Formal notice, {player}: the penalty clause in our deal has been enforced following the last result. Check your wallet for the adjustment. The fastest way to stop these letters is on track. {brand}, partnerships","Aviso formal, {player}: se ha ejecutado la cl\xC3\xA1usula de penalizaci\xC3\xB3n del acuerdo tras el \xC3\xBAltimo resultado. Revisa tu cartera para el ajuste. La forma m\xC3\xA1s r\xC3\xA1pida de frenar estas cartas est\xC3\xA1 en la pista. {brand}, patrocinios"},
  {"sp_mail_card_clause","Dear {player}: under the current agreement, the latest result triggers the penalty clause. The amount has been settled against your account. We trust the coming rounds will return to the standard that earned our backing. {brand}, sponsorships","Estimado {player}: conforme al acuerdo vigente, el \xC3\xBAltimo resultado activa la cl\xC3\xA1usula de penalizaci\xC3\xB3n. El importe se ha liquidado contra tu cuenta. Confiamos en que las pr\xC3\xB3ximas citas recuperen el nivel que motiv\xC3\xB3 nuestra apuesta. {brand}, patrocinios"},
  {"sp_mail_card_expire_subj","End of the road. Thank you","Fin de etapa. Gracias"},
  {"sp_mail_card_expire_v0","Dear {player}: our agreement has reached its term and will not continue. It was a good stretch of road together. The space on your programme is free again. We hope whoever takes it knows what they're getting. {brand}, sponsorships","Estimado {player}: nuestro acuerdo ha llegado a su t\xC3\xA9rmino y no continuar\xC3\xA1. Ha sido un buen tramo de carretera juntos. El espacio de tu programa queda libre. Esperamos que quien lo ocupe sepa lo que se lleva. {brand}, patrocinios"},
  {"sp_mail_card_expire_v1","{player}: the deal closes today as scheduled. Numbers were reviewed, hands were shaken. If your trajectory keeps its slope, don't be surprised if we call again. {brand}","{player}: el acuerdo se cierra hoy seg\xC3\xBAn lo previsto. Se revisaron los n\xC3\xBAmeros, se estrecharon las manos. Si tu trayectoria mantiene la pendiente, no te extra\xC3\xB1""e que volvamos a llamar. {brand}"},
  {"sp_mail_card_expire_v2","Our sponsorship has run its course, {player}. The slot on your car is yours to fill again. Thank you for the visibility, and for the Sundays that made it easy to justify. {brand}, partnerships","Nuestro patrocinio ha cumplido su ciclo, {player}. El hueco de tu coche vuelve a ser tuyo. Gracias por la visibilidad, y por los domingos que la hicieron f\xC3\xA1""cil de justificar. {brand}, patrocinios"},
  {"sp_mail_card_expire","Dear {player}: our agreement has reached its term and will not continue. It was a good stretch of road together. The space on your programme is free again. We hope whoever takes it knows what they're getting. {brand}, sponsorships","Estimado {player}: nuestro acuerdo ha llegado a su t\xC3\xA9rmino y no continuar\xC3\xA1. Ha sido un buen tramo de carretera juntos. El espacio de tu programa queda libre. Esperamos que quien lo ocupe sepa lo que se lleva. {brand}, patrocinios"},
  // Memories UI port (of screen_memories.jsx)
  {"mem_title","Era Memories","Recuerdos de \xC3\xA9poca"},
  {"mem_sub","Relive your bloodline's races","Revive las carreras de tu linaje"},
  {"mem_family","Family tree","\xC3\x81rbol familiar"},
  {"mem_today","TODAY","HOY"},
  {"mem_you","You","T\xC3\xBA"},
  {"mem_modern","Modern era","Era moderna"},
  {"mem_played","Played this year","Jugado este a\xC3\xB1o"},
  {"mem_relic","Relic","Reliquia"},
  {"mem_crest","Crest","Escudo"},
  {"mem_inherit","Inheritance","Herencia"},
  {"mem_reward","Reward","Recompensa"},
  {"mem_first","First time","Primera vez"},
  {"mem_repeat","Repeat (1/yr)","Repetici\xC3\xB3n (1/a\xC3\xB1o)"},
  {"mem_onlymoney","Money only, less than the 1st","Solo dinero, menor que la 1\xC2\xAA"},
  {"mem_enter","Enter memory","Entrar al recuerdo"},
  {"mem_locked","Locked","Bloqueado"},
  {"mem_gridpure","Legends only","Solo leyendas"},
  {"mem_gridcohort","Legends + privateers","Leyendas + privateers"},
  {"mem_realgrid","Real grid","Parrilla real"},
  {"mem_among_legends","among legends","entre leyendas"},
  {"mem_ancestor_seat","Ancestor's seat","Asiento del antepasado"},
  {"mem_privateer","privateer","privateer"},
  {"mem_objective","Objective","Objetivo"},
  {"mem_circuit","Circuit","Circuito"},
  {"mem_cohort_extra","+ privateer cohort","+ cohorte privateer"},
  {"mem_view_calendar","View in calendar","Ver en el calendario"},
  {"mem_available_now","Available this week","Disponible esta semana"},
  {"mem_opens_in","Opens this season","Se abre esta temporada"},
  {"mem_window_past","Returns next year","Vuelve el a\xC3\xB1o que viene"},
  {"mem_date","Date","Fecha"},
  {"cal_multiclass","MULTICLASS","MULTICLASE"},
  // Championships UI port (of screen_championships.jsx)
  {"champ_title","Championships","Campeonatos"},
  {"champ_subt","Your standing in each season series","Tu posici\xC3\xB3n en cada serie de la temporada"},
  {"champ_your_pos","YOUR POSITION","TU POSICI\xC3\x93N"},
  {"champ_pts","PTS","PTS"},
  {"champ_rounds_lc","rounds","rondas"},
  {"champ_rounds_raced","rounds raced","rondas disputadas"},
  {"champ_driver","DRIVER","PILOTO"},
  {"champ_mov","MOV","MOV"},
  {"champ_last","LAST","\xC3\x9ALT"},
  {"champ_win_lg","Win","Victoria"},
  {"champ_podium_lg","Podium","Podio"},
  // Rollover UI port (of screen_career_extra.jsx's RolloverScreen)
  {"ro_title","Season end","Fin de temporada"},
  {"ro_sub","The world moves on a year","El mundo avanza un a\xC3\xB1o"},
  {"ro_season","Season","Temporada"},
  {"ro_retirements","Retirements","Retiros"},
  {"ro_promotions","Promotions","Ascensos"},
  {"ro_market","Market moves","Movimientos de mercado"},
  {"ro_years","yrs","a\xC3\xB1os"},
  {"ro_your_contract","Your contract","Tu contrato"},
  {"ro_renew","Renew","Renovar"},
  {"ro_newgoals","New objectives","Nuevos objetivos"},
  {"ro_decline","Decline","Declinar"},
  {"ro_restart_title","AMS2 restart required","Reinicio de AMS2 necesario"},
  {"ro_restart_warn","Grids have been regenerated. Restart AMS2 to apply the changes.","Las parrillas se han regenerado. Reinicia AMS2 para aplicar los cambios."},
  {"ro_confirm","Confirm and advance","Confirmar y avanzar"},
  // renewal-terms summary line (better/same/worse/none) above the 3 choice buttons
  {"ro_renew_better","Better terms offered","Te ofrecen mejores condiciones"},
  {"ro_renew_same","Same terms","Mismas condiciones"},
  {"ro_renew_worse","Worse terms offered","Te ofrecen peores condiciones"},
  {"ro_renew_none","No renewal offered","Sin oferta de renovaci\xC3\xB3n"},
  // the mandatory season insurance-premium mail-bill at rollover. The body frames
  // the cleanliness->price loop (a cleaner season pays a lower premium next year).
  {"ro_insurance_title","Insurance premium","Prima de seguro"},
  {"ro_insurance_body","Mandatory season premium, billed per contract. A cleaner season lowers next year's premium.","Prima de temporada obligatoria, por contrato. Una temporada m\xC3\xA1s limpia baja la prima del a\xC3\xB1o que viene."},
  // Tryout UI port (of screen_career_extra.jsx's TryoutScreen). The trial is a POSITION
  // race (Top-N / beat-rival), not a lap-time mini-game; verdict is binary PASS/FAIL; fail is SOFT (no threat).
  {"ty_for","Tryout for","Prueba para"},
  {"ty_title","Evaluation test","Test de evaluaci\xC3\xB3n"},
  {"ty_challenge","The challenge","El reto"},
  {"ty_stake","What's at stake","Lo que est\xC3\xA1 en juego"},
  {"ty_start","Start the trial","Empezar la prueba"},                                  // renamed verb+noun (was "Start the tryout")
  {"ty_result","Tryout result","Resultado de la prueba"},
  {"ty_passed","Passed","Superada"},
  {"ty_acceptable","Acceptable","Aceptable"},
  {"ty_failed","Failed","Fallada"},
  {"ty_offeropens","The official offer is available","La oferta oficial est\xC3\xA1 disponible"},
  {"ty_evaluating","Racing\xE2\x80\xA6","Corriendo\xE2\x80\xA6"},                       // the trial is a real race (was "Evaluating your lap…")
  {"ty_back","Back","Volver"},
  {"ty_market","Market","Mercado"},
  // position-target + soft-fail copy
  {"ty_target_topn","Finish Top %d","Acaba Top %d"},                                    // position objective (Top-N)
  {"ty_target_beat","Beat %s","Bate a %s"},                                            // position objective (beat a rival)
  {"ty_standard_pos","Below the line and the seat goes to another driver.","Por debajo y el asiento va a otro piloto."},  // the standard warning (position, not lap time)
  {"ty_fail_soft","Not this time. The seat goes elsewhere for now, but it may open up again later.","Esta vez no. El asiento va a otro por ahora, pero puede volver a abrirse m\xC3\xA1s adelante."},  // SOFT fail, NO penalty threat
  {"champs_sub","standings & series","clasificaciones y series"},
  {"soon","coming soon","pr\xC3\xB3ximamente"},
  {"champ_enrolled","ENROLLED","INSCRITO"},
  {"champ_clash","CLASH","CHOCA"},
  {"champ_dnf","DNF","DNF"},
  {"champ_enroll","ENROLL","INSCRIBIRSE"},
  {"champ_inscription","Season entry","Inscripci\xC3\xB3n"},
  {"champ_clash_warn","This weekend clashes, so the other race will be DNF","Este finde choca, as\xC3\xAD que la otra carrera ser\xC3\xA1 DNF"},
  {"champ_race_this","RACE THIS","CORRER ESTA"},
  {"champ_no_dates","No series with dates yet","A\xC3\xBAn no hay series con fechas"},
  // the calendar as the story's timeline (continuous clock + commitments + clashes + per-championship results)
  {"cal_today","TODAY","HOY"},
  {"cal_clock_sub","Continuous clock \xC2\xB7 manage your schedule","Reloj continuo \xC2\xB7 gestiona tu agenda"},
  {"cal_adv_time","ADVANCE TIME","AVANZAR TIEMPO"},
  {"cal_adv_week_sub","+1 week","+1 semana"},
  {"cal_next_event","NEXT EVENT","PR\xC3\x93XIMO EVENTO"},
  {"cal_no_event","No upcoming events","Sin eventos por delante"},
  {"champ_past","PAST","PASADA"},
  {"champ_traveled","ELSEWHERE","OTRA SEDE"},
  {"champ_upcoming","UPCOMING","PR\xC3\x93XIMA"},
  {"cal_commitments","YOUR COMMITMENTS","TUS COMPROMISOS"},
  {"cal_enrolled_n_fmt","%d enrolled \xC2\xB7 %d clashes","%d inscritas \xC2\xB7 %d choques"},
  {"cal_result_title","SEASON RESULT","RESULTADO DE TEMPORADA"},
  {"cal_champion","Champion","Campe\xC3\xB3n"},
  {"cal_your_finish","Your finish","Tu posici\xC3\xB3n"},
  {"cal_pos_fmt","P%d","P%d"},
  {"cal_dismiss","DISMISS","DESCARTAR"},
  // calendar filter bar (3 groups: status / era / discipline) + clear
  {"cal_f_state","STATUS","ESTADO"},
  {"cal_f_era","ERA","\xC3\x89POCA"},
  {"cal_f_disc","DISCIPLINE","DISCIPLINA"},
  {"cal_f_all","ALL","TODAS"},
  {"cal_f_avail","AVAILABLE","DISPONIBLES"},
  {"cal_f_locked","LOCKED","BLOQUEADAS"},
  {"cal_f_current","CURRENT","ACTUAL"},
  {"cal_f_historic","HISTORIC","HIST\xC3\x93RICO"},
  {"cal_f_clear","CLEAR","LIMPIAR"},
  {"cal_f_none","No races match these filters","Sin pruebas con estos filtros"},
  {"hub","HUB","HUB"},
  {"tree","TREE","\xC3\x81RBOL"},
  {"calendar","CALENDAR","CALENDARIO"},
  {"garage","GARAGE","GARAJE"},
  {"mode_career","CAREER MODE","MODO CARRERA"},
  {"next_race","NEXT RACE","PR\xC3\x93XIMA CARRERA"},
  {"enter","ENTER","ENTRAR"},
  {"round","ROUND","RONDA"},
  {"day","DAY","D\xC3\x8D""A"},
  {"laps","laps","vueltas"},
  {"season_driver","SEASON 1 \xC2\xB7 DRIVER","TEMPORADA 1 \xC2\xB7 PILOTO"},
  {"credits","CREDITS","CR\xC3\x89""DITOS"},
  {"races","RACES","CARRERAS"},
  {"wins","WINS","VICTORIAS"},
  {"podiums","PODIUMS","PODIOS"},
  {"tree_sub","Progress through AMS2 classes","Avanza por las clases de AMS2"},
  {"cal_title","SEASON CALENDAR","CALENDARIO DE TEMPORADA"},
  {"cal_sub","1 race per day - choose well","1 carrera por d\xC3\xAD""a \xC2\xB7 elige bien"},
  {"garage_title","GARAGE","GARAJE"},
  {"season_progress","SEASON PROGRESS","PROGRESO DE TEMPORADA"},
  {"rookie","ROOKIE","ROOKIE"},
  {"semipro","SEMI-PRO","SEMI-PRO"},
  {"pro","PRO","PRO"},
  {"elite","ELITE","ELITE"},
  {"owned_tag","OWNED","EN PROPIEDAD"},
  {"completed","COMPLETED","COMPLETADO"},
  {"classes_lc","classes","clases"},
  {"races_lc","races","carreras"},
  {"owned_lc","owned","en propiedad"},
  {"circuit","Circuit","Circuito"},
  {"sprint","Sprint","Sprint"},
  {"grid","Grid","Parrilla"},
  {"text_mode","TEXT MODE","MODO TEXTO"},
  {"livery","LIVERY RENDER","LIVERY RENDER"},
  {"livery_sel","LIVERY","DECORACI\xC3\x93N"},
  {"level","LEVEL","NIVEL"},
  {"text_size","text size","tama\xC3\xB1o texto"},
  {"stat_pwr","POWER","POTENCIA"},
  {"stat_grp","GRIP","AGARRE"},
  {"stat_bal","BALANCE","EQUILIBRIO"},
  {"supporter","SUPPORTER","SUPPORTER"},
  {"complete_reqs","Complete requirements","Completa los requisitos"},
  {"results","RESULTS","RESULTADOS"},
  {"no_result","No result yet","Sin resultado a\xC3\xBA""n"},
  {"prize","Prize","Premio"},
  //
  {"entry","Entry fee","Inscripci\xC3\xB3n"},
  {"costs","Car (rent/run)","Coche (alq./uso)"},
  {"coverage","Team coverage","Cobertura del equipo"},      // the team's covered share (+)
  {"result_bonus","Result bonus","Bono de resultado"},      // low-type Top10/Top5/win bonus (+)
  {"deductible","Deductible","Deducible"},                  // per-race insurance deductible (−), always the player's
  {"travel","Travel","Viaje"},                              // per-race A->B travel-leg cost (−), covered per type
  {"qualifier_confirm","Confirm entry","Confirmar inscripci\xC3\xB3n"},   // qualifier-gate confirm-cost CTA
  {"qualifier_insufficient","Insufficient credits","Cr\xC3\xA9" "ditos insuficientes"},  // qualifier solvency-block reason
  {"net","Net","Neto"},
  {"xp_gained","XP gained","XP ganada"},
  {"phase_unlocked","PHASE UNLOCKED","FASE DESBLOQUEADA"},
  {"continue","CONTINUE","CONTINUAR"},
  // Hub UI ----
  {"reputation","REPUTATION","REPUTACI\xC3\x93N"},
  {"fanbase","FANBASE","AFICI\xC3\x93N"},
  {"followers","followers","seguidores"},
  {"driver_card","DRIVER CARD","FICHA DE PILOTO"},
  {"next_milestone","NEXT MILESTONE","PR\xC3\x93XIMO HITO"},
  {"to_tier_seek","to %s \xC2\xB7 top teams will seek you","para %s \xC2\xB7 los mejores equipos te buscar\xC3\xA1n"},
  // Results UI ----
  {"res_timing","TIMING","TIEMPOS"},
  {"res_total_time","Total time","Tiempo total"},
  {"res_best_lap","Best lap","Mejor vuelta"},
  {"res_gap_leader","Gap to leader","Diferencia al l\xC3\xAD""der"},
  {"res_leader","Leader","L\xC3\xAD""der"},
  {"res_money","MONEY","DINERO"},
  {"res_progression","PROGRESSION","PROGRESI\xC3\x93N"},
  {"res_balance","BALANCE","BALANCE"},
  {"res_single","Single race, no championship","Prueba \xC3\xBAnica, sin campeonato"},
  {"res_champ_standing","CHAMPIONSHIP STANDING","CLASIFICACI\xC3\x93N DEL CAMPEONATO"},
  {"res_after_race","after this race","tras esta carrera"},
  {"res_position","POSITION","POSICI\xC3\x93N"},
  {"prog_elo","ELO","ELO"},
  {"prog_rep","REPUTATION","REPUTACI\xC3\x93N"},
  {"prog_exp","EXP","EXP"},
  {"dnf","DNF","DNF"},
  {"phase","PHASE","FASE"},
  {"choose_car","CHOOSE CAR","ELEGIR COCHE"},
  {"buy","BUY","COMPRAR"},
  {"rent","RENT","ALQUILAR"},
  {"cancel","CANCEL","CANCELAR"},
  {"buy_hint","One-time \xC2\xB7 the car is yours","Pago \xC3\xBAnico \xC2\xB7 el coche es tuyo"},
  {"rent_hint","Per race \xC2\xB7 consumables & insurance","Por carrera \xC2\xB7 consumibles y seguro"},
  {"insufficient","Not enough credits","Saldo insuficiente"},
  // entry COSTS in the summary + reason for the "no money" gate
  {"inscription","Season entry","Inscripci\xC3\xB3n"},
  {"entry_fee","Race entry","Cuota de entrada"},
  {"need_credits","Need","Necesitas"},
  {"prize_label","PRIZE","PREMIO"},
  // breakdown of the PER-RACE COSTS panel (real components)
  {"costs_per_race","PER-RACE COSTS","COSTES / CARRERA"},
  {"cost_tyres","Tyres","Neum\xC3\xA1ticos"},
  // (08/26): a RENTAL row in the pre-race breakdown. Before, the panel listed the components of the
  // player's OWN car even when renting, and the rows didn't add up to YOU PAY (reported: 8,350 vs 5,051).
  {"cost_rent","Car rental (all-in)","Alquiler del coche (todo incluido)"},
  {"cost_fuel","Fuel","Combustible"},
  {"cost_fluids","Fluids","Fluidos"},
  {"cost_insurance","Insurance","Seguro"},
  {"cost_travel","Travel","Viaje"},
  {"subtotal","Subtotal","Subtotal"},
  {"covered_by_team","Covered (team/spon.)","Cubierto (equipo/pat.)"},
  {"you_pay","YOU PAY","T\xC3\x9A PAGAS"},
  {"cost_total","TOTAL","TOTAL"},
  {"free_measurement","Free (calibration)","Gratis (medici\xC3\xB3n)"},
  {"rent_bundles_note","Renting bundles consumables + insurance","Alquilar incluye consumibles + seguro"},
  {"per_race_lc","/ race","/ carrera"},
  {"veh_owned","YOUR GARAGE","TU GARAJE"},
  {"veh_market","AVAILABLE FOR THIS EVENT","DISPONIBLES PARA ESTA PRUEBA"},
  {"price","CAR PRICE","PRECIO COCHE"},   // disambiguates from PRIZE: this is the car's PURCHASE price
  {"spec","SPEC SHEET","FICHA TECNICA"},
  {"rent_note","% of car value \xC2\xB7 ins.+cons. incl.","% del valor del coche \xC2\xB7 seg.+cons. incl."},
  {"own_cost","If owned / race","Si propio / carrera"},
  {"own_cost_note","insurance + consumables","seguro + consumibles"},
  {"race_with","RACE THIS CAR","CORRER CON ESTE COCHE"},
  {"by_contract","BY CONTRACT","POR CONTRATO"},
  {"champ_by_invite","BY INVITATION","POR INVITACI\xC3\x93N"},
  {"lock_calibration","ASSESSMENT: ALL FIXED","MEDICI\xC3\x93N: TODO FIJO"},
  {"lock_contract","SET BY CONTRACT","FIJADO POR CONTRATO"},
  {"lock_team_livery","CHAMPIONSHIP LIVERY","LIBREA DEL CAMPEONATO"},
  {"lock_tryout","TRYOUT: TEAM CAR","PRUEBA: COCHE DEL EQUIPO"},
  {"locked","LOCKED","BLOQUEADO"},
  {"salary_per_race","Factory salary / race","Salario de f\xC3\xA1" "brica / carrera"},
  {"factory_salary_note","seat costs covered by the team","costes del asiento a cargo del equipo"},
  {"factory_need_rep","Requires Title reputation (REP 400)","Requiere reputaci\xC3\xB3n Title (REP 400)"},
  {"salary","SALARY","SALARIO"},
  {"duration_sel","RACE LENGTH","DURACI\xC3\x93N DE CARRERA"},
  {"dur_real","REAL","REAL"},
  {"race_len_full","FULL","COMPLETA"},
  {"dur_min","min","min"},
  {"need_car","You need a car in this class","Necesitas un coche de esta clase"},
  {"choose_car_first","Choose a race first","Elige primero una carrera"},
  {"unlocked","AVAILABLE","DISPONIBLE"},
  {"locked","LOCKED","BLOQUEADO"},
  {"missed","NOT RACED","NO DISPUTADA"},
  {"one_per_day","1/day","1/dia"},
  {"feature","Feature","Feature"},
  {"endurance","Endurance","Resistencia"},
  {"season","SEASON","TEMPORADA"},
  {"cal_real_sub","Real annual calendar \xC2\xB7 series overlap \xC2\xB7 race within your phase","Calendario anual real \xC2\xB7 las series solapan \xC2\xB7 corre dentro de tu fase"},
  {"hide_locked","hide locked","ocultar bloqueados"},
  {"all_disc","ALL","TODAS"},
  {"back","BACK","VOLVER"},
  {"season_title","REAL SERIES \xC2\xB7 SEASON","SERIES REALES \xC2\xB7 TEMPORADA"},
  {"season_pick","Pick a series for your tier","Elige una serie de tu nivel"},
  {"season_none","No series available yet \xC2\xB7 keep progressing","A\xC3\xBAn no hay series \xC2\xB7 sigue progresando"},
  {"season_round","Round","Ronda"},
  {"season_year","Year","A\xC3\xB1o"},
  {"season_continue","CONTINUE SEASON","CONTINUAR TEMPORADA"},
  {"season_abandon","ABANDON","ABANDONAR"},
  {"season_calendar","CALENDAR","CALENDARIO"},
  {"season_standings","STANDINGS","CLASIFICACI\xC3\x93N"},
  {"season_no_rounds","No rounds raced yet","Sin rondas disputadas a\xC3\xBAn"},
  {"season_run_next","RUN NEXT ROUND","CORRER SIGUIENTE RONDA"},
  {"season_done","SEASON COMPLETE","TEMPORADA TERMINADA"},
  {"season_final_pos","Final position","Posici\xC3\xB3n final"},
  {"season_back_select","BACK TO SERIES","VOLVER A SERIES"},
  {"champ_my","MY CHAMPIONSHIPS","MIS CAMPEONATOS"},
  {"champ_my_sub","Series you're enrolled in \xC2\xB7 race them from the calendar","Series en las que est\xC3\xA1s inscrito \xC2\xB7 c\xC3\xB3rrelas desde el calendario"},
  {"champ_none_enrolled","Not enrolled in any series yet \xC2\xB7 go to the calendar","A\xC3\xBAn no est\xC3\xA1s inscrito en ninguna serie \xC2\xB7 ve al calendario"},
  {"champ_pts","pts","pts"},
  {"pos_yours","YOUR POSITION","TU POSICI\xC3\x93N"},
  {"driver_hdr","DRIVER","PILOTO"},
  {"gate_req","Requires","Requiere"},
  {"gate_supl","Requires superlicense","Requiere superlicencia"},
  {"access_title","ACCESS \xC2\xB7 2 WAYS IN","ACCESO \xC2\xB7 2 V\xC3\x8D" "AS"},
  // tester note 29/08: bare "QUALIFIER"/"SEASON" said neither WHAT they are nor that the test must be WON
  {"via_qualifier","WIN THE TEST","GANA EL TEST"},
  {"via_season","BY SEASON RESULT","POR RESULTADO DE TEMPORADA"},
  {"gate_win_note","WIN the race \xC2\xB7 max AI","GANA la prueba \xC2\xB7 IA al m\xC3\xA1ximo"},
  {"gate_season_fmt","Finish top-%d in a %s-class season","Termina top-%d una temporada de clase %s"},
  {"gate_pending","pending","pendiente"},
  {"gate_in_cal","in the calendar:","en el calendario:"},
  {"gate_context_fmt","currently P%d in %s","vas P%d en %s"},
  {"sec_available","AVAILABLE","DISPONIBLES"},
  {"sec_locked","LOCKED","BLOQUEADOS"},
  {"legend_win","Win","Victoria"},
  {"legend_podium","Podium","Podio"},
  {"legend_note","big = position \xC2\xB7 small = points","N\xC2\xBA grande = posici\xC3\xB3n \xC2\xB7 N\xC2\xBA peque\xC3\xB1o = puntos"},
  {"see_class_races","GO TO NEXT RACE","IR A LA PR\xC3\x93XIMA CARRERA"},
  {"rounds_lc","rounds","rondas"},
  {"champ_leader","LEADER","L\xC3\x8D" "DER"},
  // Driver Market ----
  {"market_tab","MARKET","MERCADO"},
  {"mkt_title","DRIVER MARKET","MERCADO DE PILOTOS"},
  {"mkt_sub","Team offers and open seats","Ofertas de equipos y plazas libres"},
  {"mkt_reputation","REPUTATION","REPUTACI\xC3\x93N"},
  {"mkt_seek_you","seeking you","te buscan"},
  {"mkt_open_seats","open seats","plazas abiertas"},
  {"mkt_team_seeks","SEEKING YOU","TE BUSCAN"},
  {"mkt_driver_wanted","DRIVER WANTED","SE BUSCA PILOTO"},
  {"mkt_accept","ACCEPT CONTRACT","ACEPTAR CONTRATO"},
  {"mkt_apply","APPLY","POSTULARME"},
  {"mkt_decline","DECLINE","RECHAZAR"},
  {"mkt_accepted","CONTRACT ACCEPTED","CONTRATO ACEPTADO"},
  {"mkt_applied","APPLICATION SENT","CANDIDATURA ENVIADA"},
  {"mkt_declined","DECLINED","RECHAZADA"},
  {"mkt_sign_bonus","SIGNING BONUS","PRIMA A LA FIRMA"},
  {"mkt_per_race","PER RACE","POR CARRERA"},
  {"mkt_seat","TEAM CAR","COCHE DEL EQUIPO"},
  {"mkt_funding","FUNDING","FINANCIACI\xC3\x93N"},
  {"mkt_fund_salaried","Salaried seat","Asiento con sueldo"},
  {"mkt_fund_customer","Self-funded","Autofinanciado"},
  {"mkt_fund_paydriver","Pay-driver (you pay)","Pay-driver (pagas t\xC3\xBA)"},
  {"mkt_provides_car","The team provides the car","El equipo aporta el coche"},
  {"mkt_req_rep","REPUTATION REQUIRED","REPUTACI\xC3\x93N REQUERIDA"},
  {"mkt_rep_note","Win races and reach podiums so top teams come looking for you.","Gana carreras y sube al podio para que los mejores equipos te busquen."},
  {"mkt_next","Next","Siguiente"},
  {"mkt_to_for","for","para"},
  {"mkt_commitment","Commitment","Compromiso"},
  {"scope_season","Full season","Temporada completa"},
  {"scope_championship","Championship","Campeonato"},
  {"scope_special","Special event","Evento especial"},
  {"scope_program","Race programme","Programa de carreras"},
  // Mail ----
  {"inbox_sub","Communications with sponsors and team","Comunicaciones con patrocinadores y equipo"},
  {"inbox_unread","unread","sin leer"},
  {"tag_sponsor","SPONSOR","PATROCINADOR"},
  {"tag_team","TEAM","EQUIPO"},
  {"tag_official","OFFICIAL","OFICIAL"},
  {"inbox_offer","Offer","Oferta"},
  {"inbox_action","Action","Acci\xC3\xB3n"},
  {"inbox_answered","Answered","Respondido"},
  {"inbox_decision","Your decision","Tu decisi\xC3\xB3n"},
  {"inbox_you_chose","You chose","Elegiste"},
  {"mkt_view_market","View in Market","Ver en el Mercado"},
  {"mkt_offer_inbox","Contract offer","Oferta de contrato"},
  // dynamic offers (held / conflict / expired) ----
  {"mkt_held","COURTED","TE CORTEJAN"},                          // chip for a blocked (held) offer
  {"mkt_locked","LOCKED","BLOQUEADA"},
  {"mkt_conflict","CLASH","CHOCA"},                              // chip for a calendar conflict
  {"mkt_conflict_with","Clashes with","Choca con"},             // prefix: "Clashes with: <series>"
  {"mkt_confirm_accept","Sign anyway. The other race will be DNF","Firmar igual. La otra carrera ser\xC3\xA1 DNF"},
  {"mkt_confirm","CONFIRM","CONFIRMAR"},
  {"mkt_cancel","CANCEL","CANCELAR"},
  // SOFT "you can't yet afford this" marker (signalling only). Plumbing DORMANT in
  // (money_norm=0, no pay-seat is ever generated) -> never fires live; only the Accept button reads it.
  {"mkt_cant_afford","Can't afford yet","A\xC3\xBAn no te lo puedes permitir"},
  // polish fix: the MarketUiContractLabel "N contracts" header + per-contract kind labels were hardcoded
  // in Spanish (leaked under EN). Route them through TR() (the bridge in career_overlay.cpp resolves these).
  {"mkt_contracts_n","contracts","contratos"},                  // plural header: "<N> contracts · SeriesA · ..."
  {"mkt_kind_works","Works","Oficial"},                          // contract kind 0 (factory/works seat)
  {"mkt_kind_customer","Customer","Cliente"},                    // contract kind 1 (self-funded customer seat)
  {"mkt_kind_paydriver","Pay-driver","Pay-driver"},              // contract kind 2 (you pay)
  // the "you missed it" mail family was REMOVED entirely (inbox_missed/_body retired).
  // Expiry is silent (no mail); expired_offers now exists only as the Market's dedup set.
  // voice + templates for the generated PROGRESS/MILESTONE mail (payload_key -> TR).
  {"inbox_paddock","Paddock","Paddock"},                                 // sender for the progress newsletters
  {"inbox_sponsor","Your sponsor","Tu patrocinador"},                    // sender for sponsor mail (family=2)
  // negotiation redesign: sender for the player's SYNTHESIZED entries in the thread (never persisted).
  {"inbox_you","You","T\xC3\xBA"},
  // living-mail system: the opening arc — ob_intro (the rest of the catalog lives in the catalog task).
  {"ob_intro_subj","So this is real","Esto va en serio"},
  {"ob_intro_v0","{player}, I'm Atenea Montero. As of today, I run your career.\n\nI don't hand out seats; I open doors, and results keep them open. Everything that matters lands here: offers, milestones, the decisions only you can make.\n\nI don't write to fill your screen, so read what I send. Atenea","{player}, soy Atenea Montero. Desde hoy llevo tu carrera.\n\nNo reparto asientos; abro puertas, y los resultados las mantienen abiertas. Todo lo importante llega aqu\xC3\xAD: ofertas, hitos, y las decisiones que solo t\xC3\xBA puedes tomar.\n\nNo escribo para llenarte la pantalla, as\xC3\xAD que lee lo que te mando. Atenea"},
  {"ob_intro_v1","Welcome, {player}. I'm your manager from here on.\n\nMy job is easy to say and hard to do: when you're fast, make sure the right people know.\n\nYours is simpler still. Be fast. We start now. Atenea","Bienvenido, {player}. Soy tu manager a partir de ahora.\n\nMi trabajo es f\xC3\xA1""cil de decir y dif\xC3\xAD""cil de hacer: cuando seas r\xC3\xA1pido, que se entere quien tiene que enterarse.\n\nEl tuyo es a\xC3\xBAn m\xC3\xA1s simple. S\xC3\xA9 r\xC3\xA1pido. Empezamos ya. Atenea"},
  {"ob_intro_v2","{player}, first message, so I'll keep it short.\n\nI've managed fast drivers and I've managed lucky ones, and the fast ones last.\n\nThis inbox is where the sport talks to you: read it, answer when I ask, ignore the noise everywhere else. Let's go to work. Atenea","{player}, primer mensaje, as\xC3\xAD que ser\xC3\xA9 breve.\n\nHe llevado a pilotos r\xC3\xA1pidos y a pilotos con suerte, y los r\xC3\xA1pidos duran.\n\nEsta bandeja es donde el deporte habla contigo: l\xC3\xA9""ela, responde cuando te lo pida, e ignora el ruido de todo lo dem\xC3\xA1s. A trabajar. Atenea"},
  // living-mail system: Atenea's complete TR catalog (16 events: subj+
  // >=2 EN/ES variants; the bare key = same text as _v0, a fallback for old saves).
  {"ob_intro","{player}, I'm Atenea Montero. As of today, I run your career.\n\nI don't hand out seats; I open doors, and results keep them open. Everything that matters lands here: offers, milestones, the decisions only you can make.\n\nI don't write to fill your screen, so read what I send. Atenea","{player}, soy Atenea Montero. Desde hoy llevo tu carrera.\n\nNo reparto asientos; abro puertas, y los resultados las mantienen abiertas. Todo lo importante llega aqu\xC3\xAD: ofertas, hitos, y las decisiones que solo t\xC3\xBA puedes tomar.\n\nNo escribo para llenarte la pantalla, as\xC3\xAD que lee lo que te mando. Atenea"},
  // Atenea introduces the Memories feature when the player unlocks the first era series (mem_intro).
  {"mem_intro_subj","The name before you","El nombre antes de ti"},
  {"mem_intro","{player}, a door just opened that most drivers never see. Your surname isn't new to a grid.\n\nYour family raced these machines when they were the pinnacle, in eras we now only revive. In your Memories you can put those same cars back on the classic circuits and add your name to the lineage.\n\nWin there and you don't just score: you honour the ones who carried the name first.\n\nAtenea","{player}, se acaba de abrir una puerta que la mayor\xC3\xAD""a de pilotos nunca ve. Tu apellido no es nuevo en una parrilla.\n\nTu familia corri\xC3\xB3 estas m\xC3\xA1quinas cuando eran la cima, en \xC3\xA9pocas que hoy solo revivimos. En tus Recuerdos puedes volver a subir esos mismos coches a los circuitos cl\xC3\xA1sicos y a\xC3\xB1""adir tu nombre al linaje.\n\nGanar all\xC3\xAD no solo punt\xC3\xBA""a: honra a quienes llevaron el apellido antes.\n\nAtenea"},
  // Atenea's birthday greeting for the player (triggered by AdvanceDays crossing the date).
  {"mail_bday_subj","One more lap around the sun","Una vuelta m\xC3\xA1s al sol"},
  {"mail_bday","{age} today, {player}. In this sport we count laps, not years, but allow me to keep track of both.\n\nWhatever the calendar says, the stopwatch doesn't care. Enjoy the day; tomorrow we're back to work.\n\nAtenea","{age} hoy, {player}. En este deporte se cuentan vueltas, no a\xC3\xB1os, pero deja que yo lleve la cuenta de las dos cosas.\n\nDiga lo que diga el calendario, al cron\xC3\xB3metro le da igual. Disfruta del d\xC3\xAD""a; ma\xC3\xB1""ana volvemos al trabajo.\n\nAtenea"},
  {"mail_bday_v0","{age} today, {player}. In this sport we count laps, not years, but allow me to keep track of both.\n\nWhatever the calendar says, the stopwatch doesn't care. Enjoy the day; tomorrow we're back to work.\n\nAtenea","{age} hoy, {player}. En este deporte se cuentan vueltas, no a\xC3\xB1os, pero deja que yo lleve la cuenta de las dos cosas.\n\nDiga lo que diga el calendario, al cron\xC3\xB3metro le da igual. Disfruta del d\xC3\xAD""a; ma\xC3\xB1""ana volvemos al trabajo.\n\nAtenea"},
  {"mail_bday_v1","Happy birthday, {player}. {age} and the stopwatch is still on your side.\n\nToday the contracts, the telemetry and the press can all wait. Blow out the candles. The season won't wait long.\n\nAtenea","Felicidades, {player}. {age} y el cron\xC3\xB3metro sigue de tu parte.\n\nHoy los contratos, la telemetr\xC3\xAD""a y la prensa pueden esperar. Sopla las velas. La temporada no espera mucho.\n\nAtenea"},
  // roles and offer body --
  {"inbox_role_manager","Your manager","Tu manager"},
  {"inbox_role_teamprincipal","Team Principal","Team Principal"},
  {"inbox_offer_subj","Contract offer: {series}","Oferta de contrato: {series}"},
  // in-game fix: the old wording used to send the player to "check the terms in the Market" as if the
  // mail body didn't already carry them -- since the negotiation redesign, this very message CARRIES the
  // terms and the buttons (Accept/Decline/Negotiate per MailActionsFor). The Market screen is still alive as
  // a full showcase (comparing against every other offer), but it's no longer the only place to act.
  {"inbox_offer_body","A seat in {series} is on the table, {player}. The terms are right here: read them and answer straight from this message.\n\nThe Market still keeps the full showcase if you want to weigh this seat against everything else out there.","Hay un asiento de {series} sobre la mesa, {player}. Las condiciones est\xC3\xA1n aqu\xC3\xAD mismo: l\xC3\xA9""elas y responde directamente desde este mensaje.\n\nEl Mercado sigue con el escaparate completo por si quieres compararla con el resto."},
  // 0.4.0 renewals/rival bids (in-game fix, 07/21): dedicated templates — the renewal + 2 rival bids arrive
  // together at rollover, and with the generic template they read as three identical emails; they're also signed FROM the mail itself, not the Market.
  {"inbox_renew_subj","Renewal: {series}","Renovaci\xC3\xB3n: {series}"},
  {"inbox_renew_body","{player}, we want you at {team} next season too. Our renewal terms are in this letter: accept or decline right here.\n\nThis offer is between you and us; it never goes through the market.","{player}, queremos que sigas en {team} tambi\xC3\xA9n la pr\xC3\xB3xima temporada. Los t\xC3\xA9rminos de la renovaci\xC3\xB3n van en este correo: acepta o rechaza aqu\xC3\xAD mismo.\n\nEsta oferta es entre t\xC3\xBA y nosotros; no pasa por el mercado."},
  {"inbox_renew_early_subj","Early renewal: {series}","Renovaci\xC3\xB3n anticipada: {series}"},
  {"inbox_renew_early_body","The season is not over, {player}, but {team} would rather not wait: this renewal is priced on where you stand today. Sign now and the seat is yours. Wait for the final result and the terms can improve or worsen with it.","La temporada no ha terminado, {player}, pero en {team} preferimos no esperar: esta renovaci\xC3\xB3n va valorada por tu posici\xC3\xB3n actual. Firma ya y el asiento es tuyo. Si esperas al resultado final, los t\xC3\xA9rminos pueden mejorar o empeorar con \xC3\xA9l."},
  {"inbox_rival_subj","{team} wants to sign you","{team} quiere ficharte"},
  {"inbox_rival_body","Your season in {series} did not go unnoticed, {player}. {team} is putting money on the table to pull you out of your current seat. Read the terms and answer from this letter.","Tu temporada en {series} no ha pasado desapercibida, {player}. {team} pone dinero sobre la mesa para sacarte de tu asiento actual. Lee los t\xC3\xA9rminos y responde desde este mismo correo."},
  // negotiation redesign: NEGOTIATION-family mail (family=3) -- 11 beats x (subject + 3
  // tones v0=formal v1=warm v2=curt). Sender: career_nego_thread.cpp::EmitNegoMail.
  {"nego_accept_subj","Agreement reached: {series}","Acuerdo cerrado: {series}"},
  {"nego_accept_v0","We accept your terms for the {series} seat.\n\nThe paperwork is ready when you are. We are holding the seat for {reserve_w} week(s).","Aceptamos tus t\xC3\xA9rminos para el asiento de {series}.\n\nLos papeles est\xC3\xA1n listos cuando t\xC3\xBA lo est\xC3\xA9s. Te reservamos el asiento {reserve_w} semana(s)."},
  {"nego_accept_v1","Good news, {player}: we are in.\n\nCome sign for {series} whenever suits you this week. We will hold the seat for {reserve_w} week(s).","Buenas noticias, {player}: estamos dentro.\n\nP\xC3\xA1sate a firmar por {series} cuando te venga bien esta semana. Te guardamos el asiento {reserve_w} semana(s)."},
  {"nego_accept_v2","Terms accepted.\n\nSign within {reserve_w} week(s) or the seat goes back on the market.","T\xC3\xA9rminos aceptados.\n\nFirma en {reserve_w} semana(s) o el asiento vuelve al mercado."},
  {"nego_counter_subj","Counter-offer: {series}","Contraoferta: {series}"},
  {"nego_counter_v0","We cannot meet everything you asked.\n\nAttached is what we can do for {series}. Review it and let us know.","No podemos llegar a todo lo que pides.\n\nEsto es lo que podemos ofrecer por {series}. Rev\xC3\xADsalo y nos dices."},
  {"nego_counter_v1","You drive a hard bargain, {player}.\n\nHere is our side of it for {series}. Closer than you think.","Aprietas fuerte, {player}.\n\nAqu\xC3\xAD va nuestra parte por {series}. M\xC3\xA1s cerca de lo que crees."},
  {"nego_counter_v2","Not at that price.\n\nOur counter for {series} is on the table.","A ese precio no.\n\nNuestra contra por {series} est\xC3\xA1 sobre la mesa."},
  {"nego_ultimatum_subj","Final offer: {series}","Oferta final: {series}"},
  {"nego_ultimatum_v0","We have gone as far as we can.\n\nThe original terms for {series} stand. Accept them or we move on.","Hemos llegado tan lejos como podemos.\n\nLos t\xC3\xA9rminos originales por {series} se mantienen. Ac\xC3\xA9ptalos o pasamos p\xC3\xA1gina."},
  {"nego_ultimatum_v1","{player}, we want you in the car, but this is the line.\n\nOriginal terms. Yes or no.","{player}, te queremos en el coche, pero aqu\xC3\xAD est\xC3\xA1 el l\xC3\xADmite.\n\nT\xC3\xA9rminos originales. S\xC3\xAD o no."},
  {"nego_ultimatum_v2","Last call. Original package for {series}.\n\nTake it or leave it.","\xC3\x9Altima llamada. Paquete original por {series}.\n\nLo tomas o lo dejas."},
  {"nego_withdraw_subj","Offer withdrawn: {series}","Oferta retirada: {series}"},
  {"nego_withdraw_v0","We are withdrawing our offer for {series}. The distance between our positions is too large.\n\nWe wish you a good season.","Retiramos nuestra oferta por {series}. La distancia entre posiciones es demasiado grande.\n\nTe deseamos buena temporada."},
  {"nego_withdraw_v1","Sorry, {player}. This one is not happening.\n\nWe are pulling the {series} offer. Paths may cross again.","Lo siento, {player}. Esta vez no va a poder ser.\n\nRetiramos la oferta de {series}. Los caminos pueden volver a cruzarse."},
  {"nego_withdraw_v2","Offer withdrawn. We are done here.","Oferta retirada. Aqu\xC3\xAD terminamos."},
  {"nego_seatfilled_subj","Seat filled: {series}","Asiento cubierto: {series}"},
  {"nego_seatfilled_v0","While we talked, another driver signed for the {series} seat.\n\nThe offer is off the table.","Mientras habl\xC3\xA1""bamos, otro piloto firm\xC3\xB3 por el asiento de {series}.\n\nLa oferta queda fuera de la mesa."},
  {"nego_seatfilled_v1","Bad timing, {player}: the {series} seat has just been taken.\n\nNothing personal. Racing moves fast.","Mal momento, {player}: el asiento de {series} acaba de ocuparse.\n\nNada personal. Esto va r\xC3\xA1pido."},
  {"nego_seatfilled_v2","The {series} seat is gone. Someone else signed.","El asiento de {series} ya est\xC3\xA1 cubierto. Firm\xC3\xB3 otro."},
  {"nego_pressure_subj","We need an answer: {series}","Necesitamos respuesta: {series}"},
  {"nego_pressure_v0","Another driver is interested in the {series} seat.\n\nWe need your answer within {deadline_w} week(s).","Hay otro piloto interesado en el asiento de {series}.\n\nNecesitamos tu respuesta en {deadline_w} semana(s)."},
  {"nego_pressure_v1","{player}, honesty first: someone else is calling about {series}.\n\nYou have {deadline_w} week(s), then we listen to them.","{player}, con franqueza: hay otro preguntando por {series}.\n\nTienes {deadline_w} semana(s); despu\xC3\xA9s les escuchamos."},
  {"nego_pressure_v2","There is another candidate. {deadline_w} week(s).\n\nThe clock is running.","Hay otro candidato. {deadline_w} semana(s).\n\nEl reloj corre."},
  {"nego_reminder_subj","Our counter is waiting: {series}","Nuestra contra espera: {series}"},
  {"nego_reminder_v0","Our counter-offer for {series} is still on the table.\n\nSilence does not improve it.","Nuestra contraoferta por {series} sigue sobre la mesa.\n\nEl silencio no la mejora."},
  {"nego_reminder_v1","Still thinking it over, {player}?\n\nThe {series} package will not wait forever.","\xC2\xBFSigues d\xC3\xA1ndole vueltas, {player}?\n\nEl paquete de {series} no esperar\xC3\xA1 para siempre."},
  {"nego_reminder_v2","No reply received.\n\nThe counter for {series} expires with the offer.","Sin respuesta.\n\nLa contra por {series} caduca con la oferta."},
  {"nego_sweetener_subj","We moved for you: {series}","Nos hemos movido por ti: {series}"},
  {"nego_sweetener_v0","We have reconsidered: everything you asked for {series} is granted.\n\nThe season is close. Let us close this.","Lo hemos reconsiderado: todo lo que pediste por {series} queda concedido.\n\nLa temporada est\xC3\xA1 cerca. Cerremos esto."},
  {"nego_sweetener_v1","{player}, we want this done. Full package, as you asked.\n\nSay yes and the {series} car is yours.","{player}, queremos cerrarlo. Paquete completo, como pediste.\n\nDi que s\xC3\xAD y el coche de {series} es tuyo."},
  {"nego_sweetener_v2","Fine. Your terms. Sign for {series}.","Est\xC3\xA1 bien. Tus t\xC3\xA9rminos. Firma por {series}."},
  {"nego_reserve_expired_subj","Reservation lapsed: {series}","Reserva caducada: {series}"},
  {"nego_reserve_expired_v0","We agreed terms for {series} and you never signed.\n\nThe reservation is void and the offer is withdrawn.","Acordamos t\xC3\xA9rminos por {series} y nunca firmaste.\n\nLa reserva queda anulada y la oferta retirada."},
  {"nego_reserve_expired_v1","{player}, we shook hands and heard nothing.\n\nThe {series} seat is back on the market. Disappointing.","{player}, nos dimos la mano y no supimos m\xC3\xA1s de ti.\n\nEl asiento de {series} vuelve al mercado. Decepcionante."},
  {"nego_reserve_expired_v2","You did not sign. Deal off.","No firmaste. Trato roto."},
  {"nego_close_rollover_subj","Season turnover: {series}","Cambio de temporada: {series}"},
  {"nego_close_rollover_v0","The season has turned and our plans for {series} were remade.\n\nThe negotiation is closed. The new market is open.","La temporada ha cambiado y nuestros planes para {series} se rehicieron.\n\nLa negociaci\xC3\xB3n queda cerrada. El nuevo mercado est\xC3\xA1 abierto."},
  {"nego_close_rollover_v1","New year, new grid, {player}.\n\nThe old {series} talks are closed. Let us see what this season brings.","A\xC3\xB1o nuevo, parrilla nueva, {player}.\n\nLas conversaciones viejas de {series} quedan cerradas. Veamos qu\xC3\xA9 trae esta temporada."},
  {"nego_close_rollover_v2","Year closed. Negotiation void.","A\xC3\xB1o cerrado. Negociaci\xC3\xB3n anulada."},
  {"nego_close_elsewhere_subj","We saw the news: {series}","Vimos la noticia: {series}"},
  {"nego_close_elsewhere_v0","We understand you have signed elsewhere.\n\nWe are closing the {series} negotiation. Good luck this season.","Entendemos que has firmado en otro sitio.\n\nCerramos la negociaci\xC3\xB3n de {series}. Suerte esta temporada."},
  {"nego_close_elsewhere_v1","Congratulations on the new deal, {player}.\n\nThe {series} talks are closed. See you on track.","Enhorabuena por el nuevo contrato, {player}.\n\nLas conversaciones de {series} quedan cerradas. Nos vemos en pista."},
  {"nego_close_elsewhere_v2","Noted. Closing our file on {series}.","Anotado. Cerramos el expediente de {series}."},
  // negotiation redesign (bridge step): the PLAYER's own proposal, synthesized in the query.inbox thread
  // -- it is NEVER persisted as mail; career_api_queries.cpp::QueryInbox generates it on the fly when serving the query).
  {"nego_you_subj","Your proposal: {series}","Tu propuesta: {series}"},
  {"nego_you","You sent terms to {team}.","Enviaste t\xC3\xA9rminos a {team}."},
  // inbox redesign: offer actions inside the mail + outcomes + the courtship letter.
  {"inbox_act_accept","Accept the offer","Aceptar la oferta"},
  {"inbox_act_decline","Decline","Rechazar"},
  {"inbox_act_negotiate","Negotiate","Negociar"},
  {"inbox_res_offer_accepted","You accepted this offer. The seat is yours. The contract is now in your career.","Aceptaste esta oferta. El asiento es tuyo. El contrato ya est\xC3\xA1 en tu carrera."},
  {"inbox_res_offer_declined","You declined this offer. The team has been informed.","Rechazaste esta oferta. El equipo queda informado."},
  {"courting_subj","We are watching you: {series}","Te estamos siguiendo: {series}"},
  {"courting_v0","{player}, we like what we see. A seat in {series} with {team} could be yours.\n\nYou are not there yet, though. Keep climbing and we will talk.","{player}, nos gusta lo que vemos. Un asiento en {series} con {team} podr\xC3\xAD""a ser tuyo.\n\nA\xC3\xBAn no est\xC3\xA1s ah\xC3\xAD. Sigue subiendo y hablaremos."},
  {"courting_v1","Our scouts keep writing your name in their reports, {player}. {team} does not knock twice.\n\nEarn the license and the door to {series} opens.","Nuestros ojeadores no paran de apuntar tu nombre, {player}. {team} no llama dos veces.\n\nGana la licencia y la puerta de {series} se abre."},
  {"courting_v2","No offer today.\n\nJust so you know where you stand: {team} has a file with your name on it for {series}.","Hoy no hay oferta.\n\nSolo para que sepas d\xC3\xB3nde est\xC3\xA1s: {team} tiene una carpeta con tu nombre para {series}."},
  // ms_repband --
  {"ms_repband_subj","People are asking about you","Empiezan a preguntar por ti"},
  {"ms_repband_v0","Your name is turning up in rooms it didn't used to, {player}. Reputation moves slowly in this paddock, and you just moved it.\n\nKeep finishing races; let me handle who's asking. Atenea","Tu nombre empieza a sonar en despachos donde antes no estaba, {player}. La reputaci\xC3\xB3n se mueve despacio en este paddock, y acabas de moverla.\n\nSigue acabando carreras; de qui\xC3\xA9n pregunta me ocupo yo. Atenea"},
  {"ms_repband_v1","A scout I trust called me this morning, {player}. Nothing formal, but your name was his idea, not mine.\n\nThat's how these things always start. Atenea","Esta ma\xC3\xB1""ana me llam\xC3\xB3 un ojeador de confianza, {player}. Nada formal, pero tu nombre lo puso \xC3\xA9l, no yo.\n\nAs\xC3\xAD empiezan siempre estas cosas. Atenea"},
  {"ms_repband_v2","Reputation is the one currency here you can't buy, {player}, and yours just ticked up a notch.\n\nDon't spend it on anything foolish. It takes years to earn and one bad week to burn. Atenea","La reputaci\xC3\xB3n es la \xC3\xBAnica moneda de este paddock que no se compra, {player}, y la tuya acaba de subir un pelda\xC3\xB1o.\n\nNo la gastes en tonter\xC3\xAD""as. Cuesta a\xC3\xB1os ganarla y una mala semana quemarla. Atenea"},
  {"ms_repband","Your name is turning up in rooms it didn't used to, {player}. Reputation moves slowly in this paddock, and you just moved it.\n\nKeep finishing races; let me handle who's asking. Atenea","Tu nombre empieza a sonar en despachos donde antes no estaba, {player}. La reputaci\xC3\xB3n se mueve despacio en este paddock, y acabas de moverla.\n\nSigue acabando carreras; de qui\xC3\xA9n pregunta me ocupo yo. Atenea"},
  // ms_firstpodium --
  {"ms_firstpodium_subj","The first one doesn't come back","El primero no vuelve"},
  {"ms_firstpodium_v0","Your first podium, {player}. Look at it properly before you file it away. The first one doesn't happen twice.\n\nTomorrow is an ordinary day and the work goes on. Not today.\n\nTeams are starting to ask; leave that part to me. Atenea","Tu primer podio, {player}. M\xC3\xADralo bien antes de guardarlo. El primero no se repite.\n\nMa\xC3\xB1""ana ser\xC3\xA1 un d\xC3\xAD""a normal y el trabajo sigue. Hoy no.\n\nLos equipos empiezan a preguntar; esa parte d\xC3\xA9jamela a m\xC3\xAD. Atenea"},
  {"ms_firstpodium_v1","I've watched a lot of first podiums, {player}. The drivers who lasted treated it as a starting line, not a trophy.\n\nWhat you did at {track} travels fast in this paddock, so let it. Atenea","He visto muchos primeros podios, {player}. Los pilotos que duraron lo trataron como una l\xC3\xADnea de salida, no como un oldacre_uma401.\n\nLo de {track} corre r\xC3\xA1pido por el paddock, d\xC3\xA9jalo correr. Atenea"},
  {"ms_firstpodium_v2","Someday {track} will read well in your story, {player}. A first podium changes how people file your results: from 'promising' to 'delivering'.\n\nRest today; Sunday we go again. Atenea","Alg\xC3\xBAn d\xC3\xAD""a {track} quedar\xC3\xA1 bien en tu historia, {player}. El primer podio cambia c\xC3\xB3mo archivan tus resultados: de 'promete' a 'cumple'.\n\nHoy descansa; el domingo, otra vez. Atenea"},
  {"ms_firstpodium","Your first podium, {player}. Look at it properly before you file it away. The first one doesn't happen twice.\n\nTomorrow is an ordinary day and the work goes on. Not today.\n\nTeams are starting to ask; leave that part to me. Atenea","Tu primer podio, {player}. M\xC3\xADralo bien antes de guardarlo. El primero no se repite.\n\nMa\xC3\xB1""ana ser\xC3\xA1 un d\xC3\xAD""a normal y el trabajo sigue. Hoy no.\n\nLos equipos empiezan a preguntar; esa parte d\xC3\xA9jamela a m\xC3\xAD. Atenea"},
  // ms_firstwin --
  {"ms_firstwin_subj","You won","Has ganado"},
  {"ms_firstwin_v0","You won, {player}. I'll say it plainly, because I don't say it often: what you did at {track} is the one result every principal understands without translation.\n\nEnjoy tonight. Tomorrow we talk about what it's worth. Atenea","Has ganado, {player}. Te lo digo claro, porque no lo hago a menudo: lo de {track} es el \xC3\xBAnico resultado que cualquier director entiende sin traducci\xC3\xB3n.\n\nDisfruta esta noche. Ma\xC3\xB1""ana hablamos de lo que vale. Atenea"},
  {"ms_firstwin_v1","A first win doesn't make a career, {player}, but no career starts without one. {track} is yours now, on the record, forever.\n\nFeet on the ground; I'll take the phone calls. Atenea","Una primera victoria no hace una carrera, {player}, pero ninguna empieza sin ella. {track} ya es tuya, en acta, para siempre.\n\nPies en el suelo; las llamadas las cojo yo. Atenea"},
  {"ms_firstwin_v2","I checked the timing screen twice to be sure, {player}. A win reads differently in any paddock.\n\nOvernight you stop being a bet and start being a result.\n\nWe move carefully from here; results attract attention, and not all of it is friendly. Atenea","Mir\xC3\xA9 los tiempos dos veces para estar segura, {player}. Una victoria se lee distinto en cualquier paddock.\n\nDe la noche a la ma\xC3\xB1""ana dejas de ser una apuesta y pasas a ser un resultado.\n\nA partir de aqu\xC3\xAD, con cabeza; los resultados atraen atenci\xC3\xB3n, y no toda es amable. Atenea"},
  {"ms_firstwin","You won, {player}. I'll say it plainly, because I don't say it often: what you did at {track} is the one result every principal understands without translation.\n\nEnjoy tonight. Tomorrow we talk about what it's worth. Atenea","Has ganado, {player}. Te lo digo claro, porque no lo hago a menudo: lo de {track} es el \xC3\xBAnico resultado que cualquier director entiende sin traducci\xC3\xB3n.\n\nDisfruta esta noche. Ma\xC3\xB1""ana hablamos de lo que vale. Atenea"},
  // ms_titlefight -- the generator RETIRED this beat (career_contract.cpp) — it never fires
  // ms_titlefight_* again. The templates STAY, render-only, for old saves that already have a mail
  // with persisted subtype 4 (career_save.h:119); a later phase replaces it with a real press system.
  {"ms_titlefight_subj","Now we don't blink","Ahora no parpadeamos"},
  {"ms_titlefight_v0","P{pos} in {series} with the season almost over, {player}. Championships are lost in the head before they're lost on track.\n\nNothing changes this week: same preparation, same racing. Atenea","P{pos} en {series} con la temporada casi cerrada, {player}. Los campeonatos se pierden antes en la cabeza que en la pista.\n\nEsta semana no cambia nada: misma preparaci\xC3\xB3n, mismas carreras. Atenea"},
  {"ms_titlefight_v1","You're in the title fight, {player}. Everyone around you will start counting points out loud.\n\nDon't join them. Drive each race like it's the only one. Atenea","Est\xC3\xA1s en la pelea por el t\xC3\xADtulo, {player}. Todo el mundo a tu alrededor empezar\xC3\xA1 a contar puntos en voz alta.\n\nT\xC3\xBA no. Corre cada carrera como si fuera la \xC3\xBAnica. Atenea"},
  {"ms_titlefight_v2","Title deciders reward the calm ones, {player}. {series} can still be yours if the next Sundays are boring and clean.\n\nBoring is fine. Atenea","Los finales de campeonato premian a los tranquilos, {player}. {series} a\xC3\xBAn puede ser tuyo si los pr\xC3\xB3ximos domingos son aburridos y limpios.\n\nAburrido est\xC3\xA1 bien. Atenea"},
  {"ms_titlefight","P{pos} in {series} with the season almost over, {player}. Championships are lost in the head before they're lost on track.\n\nNothing changes this week: same preparation, same racing. Atenea","P{pos} en {series} con la temporada casi cerrada, {player}. Los campeonatos se pierden antes en la cabeza que en la pista.\n\nEsta semana no cambia nada: misma preparaci\xC3\xB3n, mismas carreras. Atenea"},
  // ms_finalseason --
  {"ms_finalseason_subj","Contract year","A\xC3\xB1o de contrato"},
  {"ms_finalseason_v0","Last season on the {team} deal, {player}. From here every result is also a negotiation.\n\nThe team is watching, and so is everyone else. Two names have already asked me about you quietly: {team_a}, {team_b}.\n\nDeliver on track; the paperwork is mine to worry about. Atenea","\xC3\x9Altima temporada del contrato con {team}, {player}. A partir de aqu\xC3\xAD, cada resultado es tambi\xC3\xA9n una negociaci\xC3\xB3n.\n\nEl equipo mira, y el resto del paddock tambi\xC3\xA9n. Ya me han preguntado por ti en voz baja dos nombres: {team_a}, {team_b}.\n\nT\xC3\xBA cumple en pista; del papeleo me preocupo yo. Atenea"},
  {"ms_finalseason_v1","Contract year in {series}, {player}. Drivers get strange in these: they overdrive, they force it.\n\nThe best answer is the boring one: clean races, full distance, every weekend. Atenea","A\xC3\xB1o de contrato en {series}, {player}. En estos a\xC3\xB1os los pilotos se enrarecen: sobreconducen, lo fuerzan.\n\nLa mejor respuesta es la aburrida: carreras limpias, distancia completa, cada fin de semana. Atenea"},
  {"ms_finalseason_v2","One year left on the current deal, {player}. I've quietly opened a couple of conversations, nothing for you to act on yet.\n\nTwo teams already know you'll be on the market: {team_a}, {team_b}.\n\nYour job is Sunday; mine is what comes after. Atenea","Queda un a\xC3\xB1o del contrato actual, {player}. He abierto un par de conversaciones discretas, nada en lo que debas actuar todav\xC3\xAD""a.\n\nDos equipos ya saben que saldr\xC3\xA1s al mercado: {team_a}, {team_b}.\n\nLo tuyo es el domingo; lo m\xC3\xAD""o, lo que viene despu\xC3\xA9s. Atenea"},
  {"ms_finalseason","Last season on the {team} deal, {player}. From here every result is also a negotiation.\n\nThe team is watching, and so is everyone else. Two names have already asked me about you quietly: {team_a}, {team_b}.\n\nDeliver on track; the paperwork is mine to worry about. Atenea","\xC3\x9Altima temporada del contrato con {team}, {player}. A partir de aqu\xC3\xAD, cada resultado es tambi\xC3\xA9n una negociaci\xC3\xB3n.\n\nEl equipo mira, y el resto del paddock tambi\xC3\xA9n. Ya me han preguntado por ti en voz baja dos nombres: {team_a}, {team_b}.\n\nT\xC3\xBA cumple en pista; del papeleo me preocupo yo. Atenea"},
  // ob_first_race --
  {"ob_first_race_subj","About your first race","Sobre tu primera carrera"},
  {"ob_first_race_v0","First one is in the books, {player}: P{pos} at {track}. Now you know what a weekend costs and what it pays. Your balance reads {credits}.\n\nResults build reputation; reputation brings offers to this inbox.\n\nThat's the whole game. The rest is detail. Atenea","Primera hecha, {player}: P{pos} en {track}. Ya sabes lo que cuesta un fin de semana y lo que paga. Tu saldo marca {credits}.\n\nLos resultados construyen reputaci\xC3\xB3n; la reputaci\xC3\xB3n trae ofertas a esta bandeja.\n\nEse es todo el juego. Lo dem\xC3\xA1s son detalles. Atenea"},
  {"ob_first_race_v1","{track}, P{pos}, written down, {player}, because everything starts somewhere.\n\nIn this sport money leaves faster than it arrives; keep an eye on your {credits} and race where it makes sense.\n\nThe Market will notice you long before it writes. Atenea","{track}, P{pos}, apuntado, {player}, porque todo empieza en alg\xC3\xBAn sitio.\n\nEn este deporte el dinero se va m\xC3\xA1s r\xC3\xA1pido de lo que entra; vigila tus {credits} y corre donde tenga sentido.\n\nEl Mercado se fijar\xC3\xA1 en ti mucho antes de escribirte. Atenea"},
  {"ob_first_race_v2","So that's a race, {player}: P{pos} at {track}, and {credits} in the bank to show for it. Feels different from the inside, doesn't it.\n\nRemember the number on the board and the one in your account; this career is decided by both.\n\nOn to the next. Atenea","As\xC3\xAD que esto es una carrera, {player}: P{pos} en {track}, y {credits} en la cuenta para demostrarlo. Se siente distinto desde dentro, \xC2\xBFverdad?\n\nRecuerda el n\xC3\xBAmero del marcador y el de tu cuenta; esta carrera se decide con los dos.\n\nA por la siguiente. Atenea"},
  {"ob_first_race","First one is in the books, {player}: P{pos} at {track}. Now you know what a weekend costs and what it pays. Your balance reads {credits}.\n\nResults build reputation; reputation brings offers to this inbox.\n\nThat's the whole game. The rest is detail. Atenea","Primera hecha, {player}: P{pos} en {track}. Ya sabes lo que cuesta un fin de semana y lo que paga. Tu saldo marca {credits}.\n\nLos resultados construyen reputaci\xC3\xB3n; la reputaci\xC3\xB3n trae ofertas a esta bandeja.\n\nEse es todo el juego. Lo dem\xC3\xA1s son detalles. Atenea"},
  // ob_first_goal --
  {"ob_first_goal_subj","Your first target","Tu primer objetivo"},
  {"ob_first_goal_v0","Time to aim at something, {player}. Your first target is simple: climb your first reputation band.\n\nNo heroics: finishes, clean laps, full races.\n\nWhen the paddock starts asking about you, you'll hear it from me first. Atenea","Toca apuntar a algo, {player}. Tu primer objetivo es simple: sube tu primera banda de reputaci\xC3\xB3n.\n\nSin heroicidades: llegadas, vueltas limpias, carreras completas.\n\nCuando el paddock empiece a preguntar por ti, te enterar\xC3\xA1s por m\xC3\xAD primero. Atenea"},
  {"ob_first_goal_v1","One goal for now, {player}: your first reputation band. Reputation is the door every offer walks through.\n\nRace often, finish always, and the rest follows on its own. Atenea","Un objetivo por ahora, {player}: tu primera banda de reputaci\xC3\xB3n. La reputaci\xC3\xB3n es la puerta por la que entra cualquier oferta.\n\nCorre a menudo, acaba siempre, y lo dem\xC3\xA1s viene solo. Atenea"},
  {"ob_first_goal_v2","Give yourself a target you can measure, {player}: the first reputation band. Not wins yet. Presence.\n\nBe the name that keeps finishing races, and the paddock does the talking for you.\n\nI'll tell you the moment it starts. Atenea","Ponte un objetivo que puedas medir, {player}: la primera banda de reputaci\xC3\xB3n. Todav\xC3\xAD""a no victorias. Presencia.\n\nS\xC3\xA9 el nombre que sigue acabando carreras, y el paddock hablar\xC3\xA1 por ti.\n\nTe aviso en cuanto empiece. Atenea"},
  {"ob_first_goal","Time to aim at something, {player}. Your first target is simple: climb your first reputation band.\n\nNo heroics: finishes, clean laps, full races.\n\nWhen the paddock starts asking about you, you'll hear it from me first. Atenea","Toca apuntar a algo, {player}. Tu primer objetivo es simple: sube tu primera banda de reputaci\xC3\xB3n.\n\nSin heroicidades: llegadas, vueltas limpias, carreras completas.\n\nCuando el paddock empiece a preguntar por ti, te enterar\xC3\xA1s por m\xC3\xAD primero. Atenea"},
  // season_preview --
  {"season_preview_subj","New season","Temporada nueva"},
  {"season_preview_v0","New season, {player}: {series}, {year}. Clean sheet.\n\nLast year's results don't score points anymore, in either direction. Watch {rival}; the timing screens say that's the reference this year.\n\nWe take it one Sunday at a time.\n\nAtenea","Temporada nueva, {player}: {series}, {year}. Hoja en blanco.\n\nLos resultados del a\xC3\xB1o pasado ya no punt\xC3\xBA""an, ni a favor ni en contra. Vigila a {rival}; los tiempos dicen que esa es la referencia este a\xC3\xB1o.\n\nVamos domingo a domingo.\n\nAtenea"},
  {"season_preview_v1","{series} {year} starts now, {player}. Preseason talk is free and worth what it costs.\n\nThe one name I'd keep in mind is {rival}: measure yourself there and the standings will take care of themselves.\n\nAtenea","Empieza {series} {year}, {player}. Las palabras de pretemporada son gratis y valen lo que cuestan.\n\nEl \xC3\xBAnico nombre que tendr\xC3\xAD""a presente es {rival}: m\xC3\xAD""dete ah\xC3\xAD y la clasificaci\xC3\xB3n se har\xC3\xA1 sola.\n\nAtenea"},
  {"season_preview_v2","Season {year} in {series}, {player}. Same job as always: arrive prepared, finish everything, let the season come to us.\n\nIf {rival} is as quick as the numbers say, we'll know soon enough.\n\nAtenea","Temporada {year} en {series}, {player}. El mismo trabajo de siempre: llegar preparado, acabarlo todo y dejar que la temporada venga a nosotros.\n\nSi {rival} es tan r\xC3\xA1pido como dicen los n\xC3\xBAmeros, lo sabremos pronto.\n\nAtenea"},
  {"season_preview","New season, {player}: {series}, {year}. Clean sheet.\n\nLast year's results don't score points anymore, in either direction. Watch {rival}; the timing screens say that's the reference this year.\n\nWe take it one Sunday at a time.\n\nAtenea","Temporada nueva, {player}: {series}, {year}. Hoja en blanco.\n\nLos resultados del a\xC3\xB1o pasado ya no punt\xC3\xBA""an, ni a favor ni en contra. Vigila a {rival}; los tiempos dicen que esa es la referencia este a\xC3\xB1o.\n\nVamos domingo a domingo.\n\nAtenea"},
  // season_close --
  {"season_close_subj","Season closed","Temporada cerrada"},
  {"season_close_v0","{series} {year} is closed, {player}. The books settle at {net} for the year. Some of it stung, some of it paid.\n\nThe standings keep the cold numbers; I keep the story. The day worth remembering was {best}. The rival who made you earn it was {nemesis}.\n\nTake a breath, then we plan the next.\n\nAtenea","{series} {year} queda cerrada, {player}. Las cuentas se saldan en {net} en el a\xC3\xB1o. Algo doli\xC3\xB3, algo pag\xC3\xB3.\n\nLa clasificaci\xC3\xB3n guarda los n\xC3\xBAmeros fr\xC3\xADos; yo guardo la historia. El d\xC3\xAD""a que merece recordarse fue {best}. El rival que te lo hizo ganar fue {nemesis}.\n\nRespira, y luego planeamos la siguiente.\n\nAtenea"},
  {"season_close_v1","Final flag on {series} {year}, {player}. Net for the year: {net}.\n\nI don't keep these letters to celebrate or to scold. I keep them so next season starts from the truth.\n\n{nemesis} pushed you all year; remember that when the schedule turns.\n\nAtenea","Bandera a cuadros en {series} {year}, {player}. Neto del a\xC3\xB1o: {net}.\n\nNo guardo estas cartas para celebrar ni para re\xC3\xB1ir. Las guardo para que la pr\xC3\xB3xima temporada arranque desde la verdad.\n\n{nemesis} te apret\xC3\xB3 todo el a\xC3\xB1o; recu\xC3\xA9rdalo cuando cambie el calendario.\n\nAtenea"},
  {"season_close_v2","That's the season, {player}: {series} {year}, done. When the accounting closes, the number is {net}.\n\nWhatever this year felt like, it's data now, and data is how we make the next one better. Your best day, {best}, is the floor we build on.\n\nAtenea","Se acab\xC3\xB3 la temporada, {player}: {series} {year}, cerrada. Cuando se cierran las cuentas, el n\xC3\xBAmero es {net}.\n\nSea lo que fuera este a\xC3\xB1o, ya es informaci\xC3\xB3n, y con informaci\xC3\xB3n se construye una mejor. Tu mejor d\xC3\xAD""a, {best}, es el suelo sobre el que construimos.\n\nAtenea"},
  // review: with neither {best} NOR {nemesis} available (a rookie with no podium and no rival yet), none of v0-v2
  // fits -- v3 only uses params that are ALWAYS available ({series}/{year}/{net}); the mail must still go out (the
  // season close is a ritual). Same "Atenea" sign-off as v0-v2 (no surname).
  {"season_close_v3","Season closed at {series}, {year} in the books. The balance: {net}.\n\nWhatever the table says, you are faster than the driver who started it. Bank that and come back hungry.\n\nAtenea","Temporada cerrada en {series}, {year} al caj\xC3\xB3n. El balance: {net}.\n\nDiga lo que diga la tabla, eres m\xC3\xA1s r\xC3\xA1pido que el piloto que la empez\xC3\xB3. Gu\xC3\xA1rdate eso y vuelve con hambre.\n\nAtenea"},
  {"season_close","{series} {year} is closed, {player}. The books settle at {net} for the year. Some of it stung, some of it paid.\n\nThe standings keep the cold numbers; I keep the story. The day worth remembering was {best}. The rival who made you earn it was {nemesis}.\n\nTake a breath, then we plan the next.\n\nAtenea","{series} {year} queda cerrada, {player}. Las cuentas se saldan en {net} en el a\xC3\xB1o. Algo doli\xC3\xB3, algo pag\xC3\xB3.\n\nLa clasificaci\xC3\xB3n guarda los n\xC3\xBAmeros fr\xC3\xADos; yo guardo la historia. El d\xC3\xAD""a que merece recordarse fue {best}. El rival que te lo hizo ganar fue {nemesis}.\n\nRespira, y luego planeamos la siguiente.\n\nAtenea"},
  // ms_hotstreak --
  {"ms_hotstreak_subj","Three in a row","Tres seguidos"},
  {"ms_hotstreak_v0","Three podiums in a row, {player}, the last at {track}.\n\nStreaks change the words the paddock uses for you: 'in form' opens more doors than 'talented' ever did.\n\nDon't change a thing. That's the whole advice. Atenea","Tres podios seguidos, {player}, el \xC3\xBAltimo en {track}.\n\nLas rachas cambian las palabras que usa el paddock contigo: 'en forma' abre m\xC3\xA1s puertas que 'con talento'.\n\nNo cambies nada. Ese es todo el consejo. Atenea"},
  {"ms_hotstreak_v1","That's a streak now, {player}. Three podiums, and {track} was the loudest of them.\n\nEnjoy it quietly. Form is a rumour until it's a whole season. Atenea","Esto ya es una racha, {player}. Tres podios, y el de {track} ha sido el que m\xC3\xA1s ruido ha hecho.\n\nDisfr\xC3\xBAtalo en voz baja. La forma es un rumor hasta que es una temporada entera. Atenea"},
  {"ms_hotstreak_v2","Three Sundays, three podiums, {player}.\n\nI don't deal in luck, so I'll name it for what it is: work showing up on the timing screens.\n\nKeep the routine exactly as it is; don't let anyone talk you into changing what's working. Atenea","Tres domingos, tres podios, {player}.\n\nNo creo en la suerte, as\xC3\xAD que lo llamar\xC3\xA9 por su nombre: trabajo asomando en los tiempos.\n\nMant\xC3\xA9n la rutina tal cual; no dejes que nadie te convenza de cambiar lo que funciona. Atenea"},
  {"ms_hotstreak","Three podiums in a row, {player}, the last at {track}.\n\nStreaks change the words the paddock uses for you: 'in form' opens more doors than 'talented' ever did.\n\nDon't change a thing. That's the whole advice. Atenea","Tres podios seguidos, {player}, el \xC3\xBAltimo en {track}.\n\nLas rachas cambian las palabras que usa el paddock contigo: 'en forma' abre m\xC3\xA1s puertas que 'con talento'.\n\nNo cambies nada. Ese es todo el consejo. Atenea"},
  // ms_coldstreak --
  {"ms_coldstreak_subj","About these last races","Sobre estas \xC3\xBAltimas carreras"},
  {"ms_coldstreak_v0","Three races without pace, {player}, and {track} was the roughest. I won't dress it up. This is how seats get lost.\n\nMy read: the car isn't the problem.\n\nFinish the next one clean, anywhere on the sheet, and the rest comes back on its own. Atenea","Tres carreras sin ritmo, {player}, y la de {track} fue la peor. No te lo voy a maquillar. As\xC3\xAD se pierden asientos.\n\nMi lectura: el coche no es el problema.\n\nAcaba limpia la pr\xC3\xB3xima, donde sea en la hoja, y el resto vuelve solo. Atenea"},
  {"ms_coldstreak_v1","Rough patch, {player}. Three in a row now. Every driver I've worked with had one; the good ones shortened it by simplifying.\n\nFinish, breathe, repeat.\n\nWe don't chase results. We rebuild them. Atenea","Mala racha, {player}. Ya van tres. Todos los pilotos con los que he trabajado tuvieron una; los buenos la acortaron simplificando.\n\nAcabar, respirar, repetir.\n\nNo perseguimos resultados. Los reconstruimos. Atenea"},
  {"ms_coldstreak_v2","I've seen the last three, {player}, {track} included. No one here is panicking, but everyone is watching.\n\nOne clean finish resets the whole thing. That's the only assignment this week. Atenea","He visto las tres \xC3\xBAltimas, {player}, {track} incluida. Aqu\xC3\xAD nadie entra en p\xC3\xA1nico, pero todos est\xC3\xA1n mirando.\n\nUna llegada limpia lo reinicia todo. Ese es el \xC3\xBAnico encargo de la semana. Atenea"},
  {"ms_coldstreak","Three races without pace, {player}, and {track} was the roughest. I won't dress it up. This is how seats get lost.\n\nMy read: the car isn't the problem.\n\nFinish the next one clean, anywhere on the sheet, and the rest comes back on its own. Atenea","Tres carreras sin ritmo, {player}, y la de {track} fue la peor. No te lo voy a maquillar. As\xC3\xAD se pierden asientos.\n\nMi lectura: el coche no es el problema.\n\nAcaba limpia la pr\xC3\xB3xima, donde sea en la hoja, y el resto vuelve solo. Atenea"},
  // inbox redesign: Atenea's milestone on returning to an already-raced circuit (track_history) + a
  // TEAMOPS block from the principal (damage/cleanliness/treasury). Convention: subj + variants + bare key (=v0).
  {"track_history_subj","You have been here before","Ya has estado aqu\xC3\xAD"},
  {"track_history_v0","{track} again, {player}. You ran here in {year} and came away P{pos}.\n\nThe paddock has a long memory for places like this. Same corners; a better driver arriving at them.\n\nShow them the difference.\n\nAtenea","Otra vez {track}, {player}. Corriste aqu\xC3\xAD en {year} y acabaste P{pos}.\n\nEl paddock tiene buena memoria para sitios as\xC3\xAD. Las mismas curvas; un piloto mejor que llega a ellas.\n\nEns\xC3\xA9\xC3\xB1""ales la diferencia.\n\nAtenea"},
  {"track_history_v1","We're going back to {track}, {player}. Last time it was {year} and you finished P{pos}.\n\nTracks don't change; drivers do. Bring what you've learned since.\n\nAtenea","Volvemos a {track}, {player}. La \xC3\xBAltima vez fue {year} y acabaste P{pos}.\n\nLas pistas no cambian; los pilotos s\xC3\xAD. Trae todo lo que has aprendido desde entonces.\n\nAtenea"},
  {"track_history_v2","{track} is on the calendar again, {player}, a course you already know.\n\nIn {year} you left it P{pos}; this version of you should leave it better. History is only useful if you beat it.\n\nAtenea","{track} vuelve al calendario, {player}, un trazado que ya conoces.\n\nEn {year} lo dejaste P{pos}; esta versi\xC3\xB3n de ti deber\xC3\xAD""a dejarlo mejor. La historia solo sirve si la superas.\n\nAtenea"},
  {"track_history","{track} again, {player}. You ran here in {year} and came away P{pos}.\n\nThe paddock has a long memory for places like this. Same corners; a better driver arriving at them.\n\nShow them the difference.\n\nAtenea","Otra vez {track}, {player}. Corriste aqu\xC3\xAD en {year} y acabaste P{pos}.\n\nEl paddock tiene buena memoria para sitios as\xC3\xAD. Las mismas curvas; un piloto mejor que llega a ellas.\n\nEns\xC3\xA9\xC3\xB1""ales la diferencia.\n\nAtenea"},
  // TEAMOPS: car damage (3 escalating tones) + clean streak + a hit to the treasury. Sender: the team
  // principal (inbox_role_teamprincipal). Institutional voice ("we"), no name; closes with {team}.
  {"ops_damage_calm_subj","Repair bill: {track}","Factura de reparaci\xC3\xB3n: {track}"},
  {"ops_damage_calm_v0","{cost} in repairs after {track}, {player}. It happens; nobody races clean every weekend.\n\nThe garage has it handled. Just bring the car home in one piece next time.\n\n{team}","{cost} en reparaciones tras {track}, {player}. Pasa; nadie corre limpio todos los fines de semana.\n\nEl taller se encarga. La pr\xC3\xB3xima vez tr\xC3\xA1""enos el coche entero y ya est\xC3\xA1.\n\n{team}"},
  {"ops_damage_calm_v1","First big bill of the run, {player}: {cost} after {track}. The team absorbs it this time.\n\nRacing costs money. We'd just rather it stayed on the tyres, not the bodywork.\n\n{team}","Primera factura gorda del tramo, {player}: {cost} tras {track}. El equipo lo asume esta vez.\n\nCorrer cuesta dinero. Solo preferimos que se vaya en los neum\xC3\xA1ticos, no en la carrocer\xC3\xAD""a.\n\n{team}"},
  {"ops_damage_calm","{cost} in repairs after {track}, {player}. It happens; nobody races clean every weekend.\n\nThe garage has it handled. Just bring the car home in one piece next time.\n\n{team}","{cost} en reparaciones tras {track}, {player}. Pasa; nadie corre limpio todos los fines de semana.\n\nEl taller se encarga. La pr\xC3\xB3xima vez tr\xC3\xA1""enos el coche entero y ya est\xC3\xA1.\n\n{team}"},
  {"ops_damage_irked_subj","That's twice now: {track}","Y van dos: {track}"},
  {"ops_damage_irked_v0","Another {cost} on repairs at {track}, {player}. This is becoming a habit.\n\nThe budget isn't bottomless, and the mechanics would like a quiet weekend for once. Tidy it up.\n\n{team}","Otros {cost} en reparaciones en {track}, {player}. Esto empieza a ser costumbre.\n\nEl presupuesto no es infinito, y a los mec\xC3\xA1nicos les vendr\xC3\xAD""a bien un fin de semana tranquilo por una vez. Ll\xC3\xA9valo con m\xC3\xA1s cabeza.\n\n{team}"},
  {"ops_damage_irked_v1","We've now paid to rebuild this car twice, {player}: {cost} this time, at {track}.\n\nSpeed we can work with. Repair invoices we'd rather not frame. Give the panels a break.\n\n{team}","Ya hemos pagado por reconstruir este coche dos veces, {player}: {cost} esta vez, en {track}.\n\nCon la velocidad trabajamos. Con las facturas de chapa, preferir\xC3\xAD""amos no enmarcarlas. Dale un respiro a la carrocer\xC3\xAD""a.\n\n{team}"},
  {"ops_damage_irked","Another {cost} on repairs at {track}, {player}. This is becoming a habit.\n\nThe budget isn't bottomless, and the mechanics would like a quiet weekend for once. Tidy it up.\n\n{team}","Otros {cost} en reparaciones en {track}, {player}. Esto empieza a ser costumbre.\n\nEl presupuesto no es infinito, y a los mec\xC3\xA1nicos les vendr\xC3\xAD""a bien un fin de semana tranquilo por una vez. Ll\xC3\xA9valo con m\xC3\xA1s cabeza.\n\n{team}"},
  {"ops_damage_warn_subj","This is now a pattern: {track}","Esto ya es un patr\xC3\xB3n: {track}"},
  {"ops_damage_warn_v0","That is {cost} in repairs at {track}, {player}. Third bill this stretch.\n\nTeams read these numbers when they decide who to keep. Clean it up.","Son {cost} en reparaciones en {track}, {player}. Tercera factura seguida.\n\nLos equipos miran estos n\xC3\xBAmeros cuando deciden a qui\xC3\xA9n renovar. L\xC3\xADmpialo."},
  {"ops_damage_warn_v1","Three repair bills in a short stretch, {player}: {cost} again, this time at {track}.\n\nI'll be blunt: a clean record weighs at renewal, and yours is getting expensive to read. Bring it home intact.\n\n{team}","Tres facturas de reparaci\xC3\xB3n en poco tiempo, {player}: otra vez {cost}, esta vez en {track}.\n\nTe lo digo claro: un historial limpio pesa a la hora de renovar, y el tuyo empieza a costar de leer. Tr\xC3\xA1""elo entero a meta.\n\n{team}"},
  {"ops_damage_warn","That is {cost} in repairs at {track}, {player}. Third bill this stretch.\n\nTeams read these numbers when they decide who to keep. Clean it up.","Son {cost} en reparaciones en {track}, {player}. Tercera factura seguida.\n\nLos equipos miran estos n\xC3\xBAmeros cuando deciden a qui\xC3\xA9n renovar. L\xC3\xADmpialo."},
  {"ops_cleanstreak_subj","Not a scratch","Ni un ara\xC3\xB1""azo"},
  {"ops_cleanstreak_v0","The mechanics asked me to pass this on, {player}: not one repair bill in a while.\n\nThat's money that stays in the budget, and a habit that gets noticed at renewal. Keep it boring.\n\n{team}","Los mec\xC3\xA1nicos quieren que te lo diga, {player}: ni una factura de reparaci\xC3\xB3n en un buen rato.\n\nEso es dinero que se queda en el presupuesto, y una costumbre que se nota al renovar. Sigue as\xC3\xAD de aburrido.\n\n{team}"},
  {"ops_cleanstreak_v1","Clean run of weekends, {player}. The car comes back the way it left.\n\nThe accountants love you and the crew loves you more. Whatever you're doing, keep doing it.\n\n{team}","Racha de fines de semana limpios, {player}. El coche vuelve como sali\xC3\xB3.\n\nA los de cuentas les encantas y a los mec\xC3\xA1nicos m\xC3\xA1s. Hagas lo que hagas, no lo cambies.\n\n{team}"},
  {"ops_cleanstreak","The mechanics asked me to pass this on, {player}: not one repair bill in a while.\n\nThat's money that stays in the budget, and a habit that gets noticed at renewal. Keep it boring.\n\n{team}","Los mec\xC3\xA1nicos quieren que te lo diga, {player}: ni una factura de reparaci\xC3\xB3n en un buen rato.\n\nEso es dinero que se queda en el presupuesto, y una costumbre que se nota al renovar. Sigue as\xC3\xAD de aburrido.\n\n{team}"},
  // hub/discord 30/08: this alert WARNS that the balance doesn't cover the NEXT event ({track} and
  // {cost} are the UPCOMING race's, see career_contract.cpp ctx.next_event), and it was worded in the
  // PAST TENSE, like an invoice already charged. A player read it literally: "I've been charged for a
  // race at a circuit I haven't been to -- in fact that circuit only appears as my NEXT race".
  {"ops_treasury_subj","Money check before {track}","Cuentas antes de {track}"},
  {"ops_treasury_v0","{track} is next, {player}, and getting on that grid costs {cost}. The balance is {credits}.\n\nI'm not telling you how to drive. I'm telling you what it costs. Sort the money before race day.\n\nAtenea","Lo siguiente es {track}, {player}, y salir a esa parrilla cuesta {cost}. El saldo es {credits}.\n\nNo te digo c\xC3\xB3mo conducir. Te digo lo que cuesta. Arregla el dinero antes del d\xC3\xAD" "a de carrera.\n\nAtenea"},
  {"ops_treasury_v1","Heads up before {track}, {player}: entering costs {cost} and we have {credits}.\n\nThe entry comes out of the same pot the wages do. Find the difference before the weekend, or we sit this one out.\n\nAtenea","Aviso antes de {track}, {player}: inscribirse cuesta {cost} y tenemos {credits}.\n\nLa inscripci\xC3\xB3n sale del mismo bote que los sueldos. Busca la diferencia antes del finde, o esta nos la saltamos.\n\nAtenea"},
  {"ops_treasury","{track} is next, {player}, and getting on that grid costs {cost}. The balance is {credits}.\n\nI'm not telling you how to drive. I'm telling you what it costs. Sort the money before race day.\n\nAtenea","Lo siguiente es {track}, {player}, y salir a esa parrilla cuesta {cost}. El saldo es {credits}.\n\nNo te digo c\xC3\xB3mo conducir. Te digo lo que cuesta. Arregla el dinero antes del d\xC3\xAD" "a de carrera.\n\nAtenea"},
  // inbox redesign: two new families. INTEL = Atenea's paddock intelligence (sender
  // inbox_role_manager, signed "Atenea", reads opportunity into every market move). intel_market embeds
  // pre-formatted headlines {m1}/{m2} (delete-only interpolator): v0/v1 use BOTH, v2 uses ONLY {m1}. Usual convention:
  // subj + variants + bare key (=v0, a fallback for old saves).
  {"intel_market_subj","The market is moving","El mercado se mueve"},
  {"intel_market_v0","Two moves crossed my desk this morning, {player}.\n\n{m1}. {m2}.\n\nEvery driver who climbs leaves a seat warm behind them, and those gaps don't stay open long. I'm watching which ones open below you.\n\nAtenea","Dos movimientos me han cruzado la mesa esta ma\xC3\xB1""ana, {player}.\n\n{m1}. {m2}.\n\nCada piloto que sube deja un asiento caliente detr\xC3\xA1s, y esos huecos no duran abiertos mucho. Vigilo cu\xC3\xA1les se abren por debajo de ti.\n\nAtenea"},
  {"intel_market_v1","The board shifted overnight, {player}.\n\n{m1}. {m2}.\n\nRead these the way I do: not as gossip, but as openings. A grid that moves at the top pulls everyone up a rung. Be ready to take the step when it reaches you.\n\nAtenea","El tablero se movi\xC3\xB3 de noche, {player}.\n\n{m1}. {m2}.\n\nL\xC3\xA9""elos como los leo yo: no como cotilleo, sino como huecos. Una parrilla que se mueve arriba tira de todos un pelda\xC3\xB1o. Prep\xC3\xA1rate para dar el paso cuando te llegue.\n\nAtenea"},
  {"intel_market_v2","One move worth your attention, {player}: {m1}.\n\nThat's a seat opening up a level above where you sit now. Gaps like this close fast, and the driver who's ready when they open is the one who takes them.\n\nKeep delivering and I'll keep watching.\n\nAtenea","Un movimiento que merece tu atenci\xC3\xB3n, {player}: {m1}.\n\nEso es un asiento que se abre un nivel por encima de donde est\xC3\xA1s ahora. Los huecos as\xC3\xAD se cierran r\xC3\xA1pido, y el que est\xC3\xA1 listo cuando se abren es quien se los queda.\n\nSigue cumpliendo y yo sigo mirando.\n\nAtenea"},
  {"intel_market","Two moves crossed my desk this morning, {player}.\n\n{m1}. {m2}.\n\nEvery driver who climbs leaves a seat warm behind them, and those gaps don't stay open long. I'm watching which ones open below you.\n\nAtenea","Dos movimientos me han cruzado la mesa esta ma\xC3\xB1""ana, {player}.\n\n{m1}. {m2}.\n\nCada piloto que sube deja un asiento caliente detr\xC3\xA1s, y esos huecos no duran abiertos mucho. Vigilo cu\xC3\xA1les se abren por debajo de ti.\n\nAtenea"},
  // intel_retire: someone the player has history with retires; the {w}-{l} record is ALWAYS available.
  {"intel_retire_subj","A name is leaving","Un nombre se retira"},
  {"intel_retire_v0","{driver} is retiring, {player}. You and {driver} go back: {w}-{l} across the years you raced each other.\n\nA rival that measured you is worth more than most trophies, and the grid gets a little emptier now.\n\nBut a name leaving is a seat opening and eyes going looking. Make sure they land on you.\n\nAtenea","{driver} se retira, {player}. T\xC3\xBA y {driver} ven\xC3\xADs de lejos: {w}-{l} en los a\xC3\xB1os que os medisteis.\n\nUn rival que te tom\xC3\xB3 la medida vale m\xC3\xA1s que muchos trofeos, y la parrilla queda un poco m\xC3\xA1s vac\xC3\xAD""a.\n\nPero un nombre que se va es un asiento que se abre y miradas que buscan. Que se posen en ti.\n\nAtenea"},
  {"intel_retire_v1","That's {driver} done, {player}: {w}-{l} between you when it was on the line.\n\nI don't get sentimental often, but a good rival keeps you honest, and you're losing one.\n\nCareers end; opportunities don't wait to be mourned. There's room on the grid tonight that wasn't there yesterday.\n\nAtenea","Se acab\xC3\xB3 {driver}, {player}: {w}-{l} entre vosotros cuando estaba en juego.\n\nNo me pongo sentimental a menudo, pero un buen rival te mantiene honesto, y est\xC3\xA1s perdiendo uno.\n\nLas carreras acaban; las oportunidades no esperan al luto. Hay hueco en la parrilla esta noche que ayer no exist\xC3\xAD""a.\n\nAtenea"},
  {"intel_retire","{driver} is retiring, {player}. You and {driver} go back: {w}-{l} across the years you raced each other.\n\nA rival that measured you is worth more than most trophies, and the grid gets a little emptier now.\n\nBut a name leaving is a seat opening and eyes going looking. Make sure they land on you.\n\nAtenea","{driver} se retira, {player}. T\xC3\xBA y {driver} ven\xC3\xADs de lejos: {w}-{l} en los a\xC3\xB1os que os medisteis.\n\nUn rival que te tom\xC3\xB3 la medida vale m\xC3\xA1s que muchos trofeos, y la parrilla queda un poco m\xC3\xA1s vac\xC3\xAD""a.\n\nPero un nombre que se va es un asiento que se abre y miradas que buscan. Que se posen en ti.\n\nAtenea"},
  // THE CHRONICLE OF THE CASCADE. One press column per season with what actually
  // happened in the world: who left, who showed up, and who fell. Not seventy emails,
  // just a journalist summarizing the silly season.
  // TWO variants, chosen by CONTENT: v1 names {faller} and only fires when there IS
  // a fall. The FIRST season has none -- nobody has had time to decline yet -- so
  // without v0 the player would go without a chronicle in the very year the career starts.
  {"press_cascade_subj","The season's comings and goings","Las idas y venidas de la temporada"},
  {"press_cascade_v0","Time to take stock, {player}. {retiree} has hung up the helmet, and a paddock without that name reads a little differently.\n\nAt the other end, {rookie} turns up at {rookie_team} with no CV to speak of and a seat most drivers wait a decade for.\n\nEvery year the grid swaps a few names. Some of them you will race.","Toca hacer balance, {player}. {retiree} ha colgado el casco, y un paddock sin ese nombre se lee distinto.\n\nEn la otra punta aparece {rookie} en {rookie_team}, sin curr\xC3\xAD" "culo que ense\xC3\xB1" "ar y con un asiento que muchos esperan diez a\xC3\xB1os.\n\nCada temporada la parrilla cambia unos cuantos nombres. Con algunos vas a coincidir."},
  {"press_cascade_v1","Time to take stock, {player}. {retiree} has hung up the helmet, and a paddock without that name reads a little differently.\n\nAt the other end, {rookie} turns up at {rookie_team} with no CV to speak of and a seat most drivers wait a decade for.\n\nAnd then there is {faller}, who starts next season in {faller_to}. Nobody plans that move; it arrives when the stopwatch stops agreeing with you.","Toca hacer balance, {player}. {retiree} ha colgado el casco, y un paddock sin ese nombre se lee distinto.\n\nEn la otra punta aparece {rookie} en {rookie_team}, sin curr\xC3\xAD" "culo que ense\xC3\xB1" "ar y con un asiento que muchos esperan diez a\xC3\xB1os.\n\nY luego est\xC3\xA1 {faller}, que arranca la temporada que viene en {faller_to}. Ese movimiento no lo planea nadie: llega cuando el cron\xC3\xB3metro deja de darte la raz\xC3\xB3n."},
  {"press_cascade","Time to take stock, {player}. {retiree} has hung up the helmet, and a paddock without that name reads a little differently.\n\nAt the other end, {rookie} turns up at {rookie_team} with no CV to speak of and a seat most drivers wait a decade for.\n\nEvery year the grid swaps a few names. Some of them you will race.","Toca hacer balance, {player}. {retiree} ha colgado el casco, y un paddock sin ese nombre se lee distinto.\n\nEn la otra punta aparece {rookie} en {rookie_team}, sin curr\xC3\xAD" "culo que ense\xC3\xB1" "ar y con un asiento que muchos esperan diez a\xC3\xB1os.\n\nCada temporada la parrilla cambia unos cuantos nombres. Con algunos vas a coincidir."},
  // intel_promo: the driver you beat gets promoted to {tier}; Atenea reads it as a mirror of the player's own level.
  {"intel_promo_subj","One you beat is moving up","Sube uno al que ganaste"},
  {"intel_promo_v0","{driver} just stepped up to {tier}, {player}. Remember them: you beat that driver when it counted, and now they're a rung above you.\n\nRead it two ways: proof you belong up there too, and a reminder that the grid doesn't wait.\n\nThe ones you beat don't stay beaten. Go take your step.\n\nAtenea","{driver} acaba de subir a {tier}, {player}. Acu\xC3\xA9rdate: a ese piloto lo ganaste cuando importaba, y ahora est\xC3\xA1 un pelda\xC3\xB1o por encima de ti.\n\nL\xC3\xA9""elo de dos formas: prueba de que t\xC3\xBA tambi\xC3\xA9n perteneces ah\xC3\xAD arriba, y recordatorio de que la parrilla no espera.\n\nLos que ganaste no se quedan ganados. Ve a por tu paso.\n\nAtenea"},
  {"intel_promo_v1","{driver} is in {tier} now, {player}, the same driver you put behind you not long ago.\n\nThat's not a threat; it's a mirror. If they're ready for that level, so are you.\n\nI'd rather you arrive there on your own results than chase their tail. Keep winning the races in front of you.\n\nAtenea","{driver} ya est\xC3\xA1 en {tier}, {player}, el mismo piloto que dejaste atr\xC3\xA1s no hace mucho.\n\nNo es una amenaza; es un espejo. Si ese piloto est\xC3\xA1 listo para ese nivel, t\xC3\xBA tambi\xC3\xA9n.\n\nPrefiero que llegues por tus propios resultados antes que persiguiendo su estela. Sigue ganando las carreras que tienes delante.\n\nAtenea"},
  {"intel_promo","{driver} just stepped up to {tier}, {player}. Remember them: you beat that driver when it counted, and now they're a rung above you.\n\nRead it two ways: proof you belong up there too, and a reminder that the grid doesn't wait.\n\nThe ones you beat don't stay beaten. Go take your step.\n\nAtenea","{driver} acaba de subir a {tier}, {player}. Acu\xC3\xA9rdate: a ese piloto lo ganaste cuando importaba, y ahora est\xC3\xA1 un pelda\xC3\xB1o por encima de ti.\n\nL\xC3\xA9""elo de dos formas: prueba de que t\xC3\xBA tambi\xC3\xA9n perteneces ah\xC3\xAD arriba, y recordatorio de que la parrilla no espera.\n\nLos que ganaste no se quedan ganados. Ve a por tu paso.\n\nAtenea"},
  // RIVAL = rival drivers writing to the player in the first person (role inbox_role_rival). Raw voice, short sentences,
  // ego; NO "Atenea" sign-off (the sender is the driver, carried in from_name). HARD RULE for rival_duel_*: NEVER accuse
  // direct contact ("you hit me" is forbidden — the engine doesn't know who touched whom); a generic "dirty race" is fine.
  {"rival_duel_calm_subj","Good race","Buena carrera"},
  {"rival_duel_calm_v0","Good scrap out there, {player}. {track} was clean and it was fast. That's how it's supposed to feel.\n\nYou held every inch out there and made me work for all of it. Respect.\n\nWe'll do it again, and I'll be a tenth quicker next time. Count on it.","Buena pelea ah\xC3\xAD fuera, {player}. {track} fue limpia y fue r\xC3\xA1pida. As\xC3\xAD tiene que sentirse.\n\nDefendiste cada palmo y me hiciste sudarlo todo. Respeto.\n\nLo repetimos, y la pr\xC3\xB3xima ir\xC3\xA9 una d\xC3\xA9""cima m\xC3\xA1s r\xC3\xA1pido. Cuenta con ello."},
  {"rival_duel_calm_v1","Have to hand it to you, {player}: that was a proper duel at {track}. Wheel to wheel, no nonsense, both of us still in one piece.\n\nI don't say this to many drivers: that was fun. Enjoy it tonight.\n\nI'm coming for that position back.","Hay que reconoc\xC3\xA9rtelo, {player}: eso s\xC3\xAD que fue un duelo en {track}. Rueda con rueda, sin tonter\xC3\xAD""as, y los dos enteros.\n\nNo se lo digo a muchos: eso fue divertido. Disfr\xC3\xBAtalo esta noche.\n\nVoy a por esa posici\xC3\xB3n otra vez."},
  {"rival_duel_calm","Good scrap out there, {player}. {track} was clean and it was fast. That's how it's supposed to feel.\n\nYou held every inch out there and made me work for all of it. Respect.\n\nWe'll do it again, and I'll be a tenth quicker next time. Count on it.","Buena pelea ah\xC3\xAD fuera, {player}. {track} fue limpia y fue r\xC3\xA1pida. As\xC3\xAD tiene que sentirse.\n\nDefendiste cada palmo y me hiciste sudarlo todo. Respeto.\n\nLo repetimos, y la pr\xC3\xB3xima ir\xC3\xA9 una d\xC3\xA9""cima m\xC3\xA1s r\xC3\xA1pido. Cuenta con ello."},
  {"rival_duel_heated_subj","Next time","La pr\xC3\xB3xima"},
  {"rival_duel_heated_v0","Enjoy that one while it lasts, {player}. You got by me at {track}, fine, but you know as well as I do it was on the edge.\n\nNext time I'm not leaving you the room. You'll have to actually be faster, not just braver.\n\nSee you out there.","Disfruta esa mientras dure, {player}. Me pasaste en {track}, vale, pero sabes tan bien como yo que fue al l\xC3\xADmite.\n\nLa pr\xC3\xB3xima no te dejo el hueco. Vas a tener que ser m\xC3\xA1s r\xC3\xA1pido de verdad, no solo m\xC3\xA1s valiente.\n\nNos vemos ah\xC3\xAD fuera."},
  {"rival_duel_heated_v1","Don't get comfortable, {player}. {track} went your way, I'll give you that much. Once.\n\nI've watched the replay and I know exactly where I lost it, and it won't happen again.\n\nYou had your day. The next one's mine. Bring everything you've got.","No te acomodes, {player}. {track} cay\xC3\xB3 de tu lado, eso te lo concedo. Una vez.\n\nHe visto la repetici\xC3\xB3n y s\xC3\xA9 exactamente d\xC3\xB3nde lo perd\xC3\xAD, y no se repite.\n\nTuviste tu d\xC3\xAD""a. El siguiente es m\xC3\xADo. Trae todo lo que tengas."},
  // review: v2/v3 = the rival STAYED ahead (duel_won=false) — never "you passed me".
  {"rival_duel_heated_v2","All race in my mirrors at {track}, and that is where you stayed.\n\nChase me as long as you like. That gap is my property.","Toda la carrera en mis espejos en {track}, y ah\xC3\xAD te quedaste.\n\nPers\xC3\xADgueme lo que quieras. Ese hueco es m\xC3\xADo."},
  {"rival_duel_heated_v3","I saw you coming at {track}. Saw you, measured you, shut the door.\n\nNothing personal. The front is taken.","Te vi venir en {track}. Te vi, te tom\xC3\xA9 la medida y cerr\xC3\xA9 la puerta.\n\nNada personal. La cabeza est\xC3\xA1 ocupada."},
  {"rival_duel_heated","Enjoy that one while it lasts, {player}. You got by me at {track}, fine, but you know as well as I do it was on the edge.\n\nNext time I'm not leaving you the room. You'll have to actually be faster, not just braver.\n\nSee you out there.","Disfruta esa mientras dure, {player}. Me pasaste en {track}, vale, pero sabes tan bien como yo que fue al l\xC3\xADmite.\n\nLa pr\xC3\xB3xima no te dejo el hueco. Vas a tener que ser m\xC3\xA1s r\xC3\xA1pido de verdad, no solo m\xC3\xA1s valiente.\n\nNos vemos ah\xC3\xAD fuera."},
  {"rival_duel_beef_subj","You and me again","Otra vez t\xC3\xBA y yo"},
  {"rival_duel_beef_v0","Here we are again, {player}. You, me, and another messy race at {track}. It's always you, isn't it.\n\nI'm past being surprised by it now. This is just what we are.\n\nOne of these seasons one of us ends the other's day for good. Won't be me.","Otra vez aqu\xC3\xAD, {player}. T\xC3\xBA, yo, y otra carrera sucia en {track}. Siempre eres t\xC3\xBA, \xC2\xBFno?\n\nYa ni me sorprende. Esto es lo que hay entre nosotros.\n\nAlguna temporada uno le acaba el d\xC3\xAD""a al otro para siempre. No ser\xC3\xA9 yo."},
  {"rival_duel_beef_v1","Of course it was you at {track}, {player}. It's always you.\n\nThis is the second, third time we've turned a Sunday into a mess between us, and I've stopped pretending that's a coincidence.\n\nYou and me, we don't do clean. Fine. I stopped needing it clean a while ago.","C\xC3\xB3mo no, eras t\xC3\xBA en {track}, {player}. Siempre t\xC3\xBA.\n\nEs la segunda, la tercera vez que convertimos un domingo en un desastre entre los dos, y he dejado de fingir que es casualidad.\n\nT\xC3\xBA y yo no sabemos correr limpio. Bien. Hace tiempo que dej\xC3\xA9 de necesitarlo limpio."},
  // review: v2/v3 = a CLEAN race (race_clean=true) — pure rivalry, with NO contact accusation.
  {"rival_duel_beef_v2","You again, {player}. Every time I look at a timing screen your name is next to mine.\n\nFine. I am done sharing that piece of track.","Otra vez t\xC3\xBA, {player}. Cada vez que miro una pantalla de tiempos tu nombre est\xC3\xA1 pegado al m\xC3\xADo.\n\nVale. Se me acab\xC3\xB3 la paciencia con ese trozo de pista."},
  {"rival_duel_beef_v3","You and me for the same metre of asphalt, race after race, {player}.\n\nThis does not end in a handshake.","T\xC3\xBA y yo al mismo metro de asfalto, carrera tras carrera, {player}.\n\nEsto no acaba con un apret\xC3\xB3n de manos."},
  {"rival_duel_beef","Here we are again, {player}. You, me, and another messy race at {track}. It's always you, isn't it.\n\nI'm past being surprised by it now. This is just what we are.\n\nOne of these seasons one of us ends the other's day for good. Won't be me.","Otra vez aqu\xC3\xAD, {player}. T\xC3\xBA, yo, y otra carrera sucia en {track}. Siempre eres t\xC3\xBA, \xC2\xBFno?\n\nYa ni me sorprende. Esto es lo que hay entre nosotros.\n\nAlguna temporada uno le acaba el d\xC3\xAD""a al otro para siempre. No ser\xC3\xA9 yo."},
  // rival_nemesis_*: season closer. respect = you earned their respect (h2h in the player's favor); uses {driver}
  // as the sign-off (the rival names themself). pique = needling (h2h against, or an aggressive rival); no {driver}, closes with a jab.
  {"rival_nemesis_respect_subj","You earned it","Te lo has ganado"},
  {"rival_nemesis_respect_v0","Season's done, {player}, so I'll say it once: you had my number this year. {w}-{l} when it was the two of us. I know the count as well as you do.\n\nI don't hand respect out, but you took it fair and square. Beat me again next year and I'll say it again.\n\n{driver}","Se acab\xC3\xB3 la temporada, {player}, as\xC3\xAD que lo digo una vez: este a\xC3\xB1o me tomaste la medida. {w}-{l} cuando \xC3\xA9ramos t\xC3\xBA y yo. Me s\xC3\xA9 la cuenta tan bien como t\xC3\xBA.\n\nNo regalo respeto, pero te lo ganaste en buena ley. Vu\xC3\xA9lveme a ganar el a\xC3\xB1o que viene y te lo vuelvo a decir.\n\n{driver}"},
  {"rival_nemesis_respect_v1","One thing before the winter, {player}: you were the better of us this year, and I don't enjoy typing that. {w}-{l} in the ones that mattered.\n\nI spent all season chasing your gearbox and it taught me plenty. Rest up. I'm rebuilding over the break, and I'm coming back a problem.\n\n{driver}","Una cosa antes del invierno, {player}: este a\xC3\xB1o fuiste el mejor de los dos, y no me gusta escribirlo. {w}-{l} en las que contaban.\n\nMe he pasado la temporada persiguiendo tu caja de cambios y he aprendido un mont\xC3\xB3n. Descansa. Me reconstruyo en el par\xC3\xB3n, y vuelvo hecho un problema.\n\n{driver}"},
  {"rival_nemesis_respect","Season's done, {player}, so I'll say it once: you had my number this year. {w}-{l} when it was the two of us. I know the count as well as you do.\n\nI don't hand respect out, but you took it fair and square. Beat me again next year and I'll say it again.\n\n{driver}","Se acab\xC3\xB3 la temporada, {player}, as\xC3\xAD que lo digo una vez: este a\xC3\xB1o me tomaste la medida. {w}-{l} cuando \xC3\xA9ramos t\xC3\xBA y yo. Me s\xC3\xA9 la cuenta tan bien como t\xC3\xBA.\n\nNo regalo respeto, pero te lo ganaste en buena ley. Vu\xC3\xA9lveme a ganar el a\xC3\xB1o que viene y te lo vuelvo a decir.\n\n{driver}"},
  {"rival_nemesis_pique_subj","Not done with you","No he acabado contigo"},
  {"rival_nemesis_pique_v0","Season's over, {player}, but you and I aren't. {w}-{l} between us this year.\n\nThe numbers are the numbers, and I remember every one of them. You won't like how hard I've been working since the last flag.\n\nEnjoy the quiet. It doesn't last.","Se acab\xC3\xB3 la temporada, {player}, pero t\xC3\xBA y yo no. {w}-{l} entre nosotros este a\xC3\xB1o.\n\nLos n\xC3\xBAmeros son los n\xC3\xBAmeros, y me acuerdo de todos. No te va a gustar lo duro que estoy entrenando desde la \xC3\xBAltima bandera.\n\nDisfruta del silencio. No dura."},
  {"rival_nemesis_pique_v1","Don't get used to that scoreline, {player}. {w}-{l} this year, and every one of my losses is filed away with your name on it.\n\nYou were quicker when it counted. This year. I've got a long memory and a longer winter to train.\n\nWe settle this next season. All of it.","No te acostumbres a ese marcador, {player}. {w}-{l} este a\xC3\xB1o, y cada derrota m\xC3\xAD""a la tengo guardada con tu nombre.\n\nFuiste m\xC3\xA1s r\xC3\xA1pido cuando contaba. Este a\xC3\xB1o. Tengo buena memoria y un invierno largo para entrenar.\n\nEsto lo arreglamos la temporada que viene. Todo."},
  {"rival_nemesis_pique","Season's over, {player}, but you and I aren't. {w}-{l} between us this year.\n\nThe numbers are the numbers, and I remember every one of them. You won't like how hard I've been working since the last flag.\n\nEnjoy the quiet. It doesn't last.","Se acab\xC3\xB3 la temporada, {player}, pero t\xC3\xBA y yo no. {w}-{l} entre nosotros este a\xC3\xB1o.\n\nLos n\xC3\xBAmeros son los n\xC3\xBAmeros, y me acuerdo de todos. No te va a gustar lo duro que estoy entrenando desde la \xC3\xBAltima bandera.\n\nDisfruta del silencio. No dura."},
  // standalone role for the RIVAL sender (parallel to inbox_role_manager/_teamprincipal).
  {"inbox_role_rival","Rival driver","Piloto rival"},
  // PRESS = the engine's journalist (role inbox_role_press). A 3rd voice: professional press with an edge, neither Atenea's
  // warmth nor the rival's ego. Asks for statements/an interview after a milestone; QUESTIONS end in a question mark.
  // press_q_*: subj + v0 (NO rival) + v1 (weaves in {rival}, comparison/needling) + bare key = v0 (the emitter forces
  // v0 when there's no rival). HARD RULE: no on-track contact accusations. Tone labels = buttons (no variants).
  // press_res_*: closers for the published piece (rival-free, {player} at most). press_ranking: a preseason piece.
  {"inbox_role_press","Motorsport press","Prensa del motor"},
  // press_q_firstwin ({player}{series}{track}) --
  {"press_q_firstwin_subj","Your first win: a word?","Tu primera victoria, \xC2\xBFunas palabras?"},
  {"press_q_firstwin_v0","First win in {series}, {player}, at {track}, no less.\n\nA maiden victory in this paddock is either the start of something or a one-off that flattered to deceive, and my readers will want to know which.\n\nOn the record: which is it going to be?","Primera victoria en {series}, {player}, y nada menos que en {track}.\n\nUna primera victoria en este paddock es el principio de algo o un espejismo que enga\xC3\xB1""a, y mis lectores querr\xC3\xA1n saber cu\xC3\xA1l de las dos.\n\nPara el acta: \xC2\xBF""cu\xC3\xA1l va a ser?"},
  {"press_q_firstwin_v1","First win in {series}, {player}: {track} put your name on the trophy, and {rival} was quick to call it a fluke to anyone who'd listen.\n\nI'm giving you right of reply before I file.\n\nWas it your day, or {rival}'s off day?","Primera victoria en {series}, {player}: {track} puso tu nombre en el oldacre_uma401, y {rival} no ha tardado en llamarlo chiripa a quien quisiera o\xC3\xADrlo.\n\nTe doy derecho a r\xC3\xA9plica antes de publicar.\n\n\xC2\xBF""Fue tu d\xC3\xAD""a, o el mal d\xC3\xAD""a de {rival}?"},
  {"press_q_firstwin","First win in {series}, {player}, at {track}, no less.\n\nA maiden victory in this paddock is either the start of something or a one-off that flattered to deceive, and my readers will want to know which.\n\nOn the record: which is it going to be?","Primera victoria en {series}, {player}, y nada menos que en {track}.\n\nUna primera victoria en este paddock es el principio de algo o un espejismo que enga\xC3\xB1""a, y mis lectores querr\xC3\xA1n saber cu\xC3\xA1l de las dos.\n\nPara el acta: \xC2\xBF""cu\xC3\xA1l va a ser?"},
  // press_q_titlefight ({player}{series}{pos}) --
  {"press_q_titlefight_subj","In the title fight: your line?","En la pelea por el t\xC3\xADtulo, \xC2\xBFtu versi\xC3\xB3n?"},
  {"press_q_titlefight_v0","P{pos} in {series} with the flag in sight, {player}.\n\nTitle runs are won by the ones who hold their nerve in front of a microphone as much as behind the wheel.\n\nSo, on the record: can you close it out?","P{pos} en {series} con la bandera a la vista, {player}.\n\nLas peleas por el t\xC3\xADtulo las ganan los que aguantan el pulso ante un micr\xC3\xB3""fono tanto como al volante.\n\nAs\xC3\xAD que, para el acta: \xC2\xBFpuedes cerrarlo?"},
  {"press_q_titlefight_v1","{rival} says the pressure of P{pos} in {series} will get to you before I do, {player}.\n\nI'd rather print your version.\n\nDoes the number rattle you, or are you the one doing the rattling?","{rival} dice que la presi\xC3\xB3n de ir P{pos} en {series} podr\xC3\xA1 contigo antes que yo, {player}.\n\nPrefiero publicar tu versi\xC3\xB3n.\n\n\xC2\xBFTe pesa el n\xC3\xBAmero, o eres t\xC3\xBA quien lo hace pesar a los dem\xC3\xA1s?"},
  {"press_q_titlefight","P{pos} in {series} with the flag in sight, {player}.\n\nTitle runs are won by the ones who hold their nerve in front of a microphone as much as behind the wheel.\n\nSo, on the record: can you close it out?","P{pos} en {series} con la bandera a la vista, {player}.\n\nLas peleas por el t\xC3\xADtulo las ganan los que aguantan el pulso ante un micr\xC3\xB3""fono tanto como al volante.\n\nAs\xC3\xAD que, para el acta: \xC2\xBFpuedes cerrarlo?"},
  // press_q_titlewon ({player}{series}) --
  {"press_q_titlewon_subj","Champion: give me the headline?","Campe\xC3\xB3n, \xC2\xBFme das el titular?"},
  {"press_q_titlewon_v0","Champion of {series}, {player}.\n\nThe trophy is engraved; now the story gets written, and I'd sooner write it in your words than someone else's.\n\nWhat does this title actually mean to you?","Campe\xC3\xB3n de {series}, {player}.\n\nEl oldacre_uma401 ya est\xC3\xA1 grabado; ahora se escribe la historia, y prefiero escribirla con tus palabras antes que con las de otro.\n\n\xC2\xBFQu\xC3\xA9 significa de verdad este t\xC3\xADtulo para ti?"},
  {"press_q_titlewon_v1","You've got the {series} crown, {player}, and {rival} is already telling anyone with a notebook it should have been theirs.\n\nMy column has room for one quote from the champion.\n\nCare to answer them?","Tienes la corona de {series}, {player}, y {rival} ya le cuenta a todo el que lleve libreta que deber\xC3\xAD""a haber sido suya.\n\nMi columna tiene sitio para una frase del campe\xC3\xB3n.\n\n\xC2\xBFQuieres responderle?"},
  {"press_q_titlewon","Champion of {series}, {player}.\n\nThe trophy is engraved; now the story gets written, and I'd sooner write it in your words than someone else's.\n\nWhat does this title actually mean to you?","Campe\xC3\xB3n de {series}, {player}.\n\nEl oldacre_uma401 ya est\xC3\xA1 grabado; ahora se escribe la historia, y prefiero escribirla con tus palabras antes que con las de otro.\n\n\xC2\xBFQu\xC3\xA9 significa de verdad este t\xC3\xADtulo para ti?"},
  // press_q_joblost ({player}{team}) --
  {"press_q_joblost_subj","Out at {team}: a comment?","Fuera de {team}, \xC2\xBF""alg\xC3\xBAn comentario?"},
  {"press_q_joblost_v0","The paddock says you're out at {team}, {player}.\n\nThese stories always read badly when the driver stays silent. I'm coming to you first, before the rumour hardens into fact.\n\nWhat happened in there?","En el paddock dicen que est\xC3\xA1s fuera de {team}, {player}.\n\nEstas historias siempre se leen mal cuando el piloto calla. Vengo a ti primero, antes de que el rumor se convierta en un hecho.\n\n\xC2\xBFQu\xC3\xA9 pas\xC3\xB3 ah\xC3\xAD dentro?"},
  {"press_q_joblost_v1","Word is you're out at {team}, {player}, and {rival} isn't exactly hiding a smile about it.\n\nI don't print smirks, I print quotes.\n\nWhat's your side of it?","Se dice que est\xC3\xA1s fuera de {team}, {player}, y {rival} no disimula precisamente la sonrisa.\n\nYo no publico sonrisitas, publico declaraciones.\n\n\xC2\xBF""Cu\xC3\xA1l es tu versi\xC3\xB3n?"},
  {"press_q_joblost","The paddock says you're out at {team}, {player}.\n\nThese stories always read badly when the driver stays silent. I'm coming to you first, before the rumour hardens into fact.\n\nWhat happened in there?","En el paddock dicen que est\xC3\xA1s fuera de {team}, {player}.\n\nEstas historias siempre se leen mal cuando el piloto calla. Vengo a ti primero, antes de que el rumor se convierta en un hecho.\n\n\xC2\xBFQu\xC3\xA9 pas\xC3\xB3 ah\xC3\xAD dentro?"},
  // press_tone_*: action labels (buttons, no variants) --
  {"press_tone_humble","Answer with humility","Responder con humildad"},
  {"press_tone_spicy","Take the bait","Entrar al trapo"},
  {"press_tone_safe","Give the PR answer","Respuesta de manual"},
  // press_res_*: closers for the published piece (rival-free, {player} at most) --
  {"press_res_humble_v0","The piece ran this morning, {player}.\n\nYou answered with humility and the paddock noticed: no fireworks, just the kind of quote that earns a nod in the right rooms.\n\nQuietly, you did yourself a favour.","La pieza sali\xC3\xB3 esta ma\xC3\xB1""ana, {player}.\n\nRespondiste con humildad y el paddock lo not\xC3\xB3: sin fuegos artificiales, solo la clase de frase que arranca un gesto de aprobaci\xC3\xB3n en los despachos que importan.\n\nEn voz baja, te hiciste un favor."},
  {"press_res_humble_v1","Filed and printed, {player}.\n\nA modest line, cleanly delivered. It won't trend, but the people who matter read it and thought better of you for it.\n\nRespect, quietly earned.","Redactada y publicada, {player}.\n\nUna frase modesta, entregada con limpieza. No ser\xC3\xA1 tendencia, pero quien importa la ley\xC3\xB3 y te tuvo en mejor concepto.\n\nRespeto, ganado en voz baja."},
  {"press_res_humble","The piece ran this morning, {player}.\n\nYou answered with humility and the paddock noticed: no fireworks, just the kind of quote that earns a nod in the right rooms.\n\nQuietly, you did yourself a favour.","La pieza sali\xC3\xB3 esta ma\xC3\xB1""ana, {player}.\n\nRespondiste con humildad y el paddock lo not\xC3\xB3: sin fuegos artificiales, solo la clase de frase que arranca un gesto de aprobaci\xC3\xB3n en los despachos que importan.\n\nEn voz baja, te hiciste un favor."},
  {"press_res_spicy_v0","Your quote is out, {player}, and it's doing numbers.\n\nYou took the bait and handed me a headline. The phones are ringing and the forums are lit.\n\nWhether that helps you or haunts you, we'll both find out soon enough.","Tu frase ya est\xC3\xA1 fuera, {player}, y est\xC3\xA1 haciendo ruido.\n\nEntraste al trapo y me diste un titular. Los tel\xC3\xA9""fonos no paran y los foros arden.\n\nSi eso te ayuda o te persigue, lo sabremos pronto los dos."},
  {"press_res_spicy_v1","It ran, {player}, and it made noise: exactly the kind you can't take back.\n\nYou went in swinging and the paddock heard every word. Bold.\n\nWe'll see who's still talking about it next week.","Se public\xC3\xB3, {player}, e hizo ruido: justo del que ya no se puede retirar.\n\nEntraste a por todas y el paddock escuch\xC3\xB3 cada palabra. Valiente.\n\nVeremos qui\xC3\xA9n sigue habl\xC3\xA1ndolo la semana que viene."},
  {"press_res_spicy","Your quote is out, {player}, and it's doing numbers.\n\nYou took the bait and handed me a headline. The phones are ringing and the forums are lit.\n\nWhether that helps you or haunts you, we'll both find out soon enough.","Tu frase ya est\xC3\xA1 fuera, {player}, y est\xC3\xA1 haciendo ruido.\n\nEntraste al trapo y me diste un titular. Los tel\xC3\xA9""fonos no paran y los foros arden.\n\nSi eso te ayuda o te persigue, lo sabremos pronto los dos."},
  {"press_res_safe_v0","The interview ran, {player}.\n\nYou gave me the safe answer and I gave it the space it deserved, which wasn't much.\n\nHarmless today, forgotten by the weekend.","La entrevista sali\xC3\xB3, {player}.\n\nMe diste la respuesta de manual y le di el espacio que merec\xC3\xAD""a, que no era mucho.\n\nInofensiva hoy, olvidada para el fin de semana."},
  {"press_res_safe_v1","Filed, {player}.\n\nClean, careful, forgettable: the PR line does its job and nothing more. It leaves you exactly where you started.\n\nSometimes that's the whole strategy.","Redactada, {player}.\n\nLimpia, prudente, olvidable: la respuesta de manual cumple su funci\xC3\xB3n y nada m\xC3\xA1s. Te deja justo donde empezaste.\n\nA veces esa es toda la estrategia."},
  {"press_res_safe","The interview ran, {player}.\n\nYou gave me the safe answer and I gave it the space it deserved, which wasn't much.\n\nHarmless today, forgotten by the weekend.","La entrevista sali\xC3\xB3, {player}.\n\nMe diste la respuesta de manual y le di el espacio que merec\xC3\xAD""a, que no era mucho.\n\nInofensiva hoy, olvidada para el fin de semana."},
  // press_ranking ({rank}{leader}{player}): a preseason piece, no rival --
  {"press_ranking_subj","Preseason power rankings","Ranking de pretemporada"},
  {"press_ranking_v0","The preseason rankings are out, {player}, and I've got you at {rank}.\n\n{leader} sits top of my board, the number everyone else is chasing.\n\nRankings are opinions with a byline; prove mine wrong on track and I'll happily rewrite it.","Ya est\xC3\xA1 el ranking de pretemporada, {player}, y te pongo {rank}.\n\n{leader} encabeza mi tabla, el n\xC3\xBAmero que todos persiguen.\n\nUn ranking es una opini\xC3\xB3n con firma; d\xC3\xA9jame en evidencia en pista y lo reescribo encantado."},
  {"press_ranking_v1","My preseason grid is filed, {player}: {leader} at the summit, you at {rank}.\n\nI rank on what I've seen, not on what agents whisper.\n\nIf {rank} stings, the fix is on track, not in my inbox.","Mi parrilla de pretemporada ya est\xC3\xA1 cerrada, {player}: {leader} en lo m\xC3\xA1s alto, t\xC3\xBA en {rank}.\n\nOrdeno por lo que he visto, no por lo que susurran los agentes.\n\nSi {rank} escuece, el arreglo est\xC3\xA1 en la pista, no en mi bandeja de entrada."},
  {"press_ranking","The preseason rankings are out, {player}, and I've got you at {rank}.\n\n{leader} sits top of my board, the number everyone else is chasing.\n\nRankings are opinions with a byline; prove mine wrong on track and I'll happily rewrite it.","Ya est\xC3\xA1 el ranking de pretemporada, {player}, y te pongo {rank}.\n\n{leader} encabeza mi tabla, el n\xC3\xBAmero que todos persiguen.\n\nUn ranking es una opini\xC3\xB3n con firma; d\xC3\xA9jame en evidencia en pista y lo reescribo encantado."},
  // INVITE (family 7), the last piece of the inbox redesign. TWO voices:
  //  (a) invite_event = announcement of a SPECIAL EVENT. The mail is the PUSH channel (the event never entered the
  //      calendar: it's a queryable record from the calendar-agnostic redesign). Voice of the
  //      ORGANIZATION: institutional but warm, invites you in. Sender "Career Engine Events" goes in
  //      from_name (NOT in the body), role inbox_role_organizer. Mail actions: CTA into the existing flow
  //      (EventPrerace, where you pick class/car) + inbox_act_ignore. No "accept = arm". _subj + v0 + v1
  //      + bare key = v0. Single outcome: invite_event_res_ignored (Ignore; brief, no guilt-tripping).
  //  (b) invite_sub = a one-off SUBSTITUTION. The TEAM PRINCIPAL offers their seat for ONE round. HARD RULE:
  //      the regular is NEVER named and no injury is invented ("our regular had to withdraw from the
  //      round"). Voice: professional urgency; the fee stated plainly. _subj + v0 + v1 + bare = v0.
  //      Outcomes: invite_sub_res_accept (deal closed, car ready) / invite_sub_res_decline (understood,
  //      no hard feelings). Expiry closes with choice 0 (the standard rule): there's no invite_sub_expired.
  {"inbox_role_organizer","Event organiser","Organizaci\xC3\xB3n del evento"},
  // invite_event ({player}{title}{reward}) --
  {"invite_event_subj","You're invited: {title}","Est\xC3\xA1s invitado: {title}"},
  {"invite_event_v0","{player}, the organisers of {title} are opening the gates to a select few this window, and your name is on the list.\n\nIt's a one-off, run for the prestige and a purse worth {reward}.\n\nThe invitation is yours to take. We'd be glad to see you on the grid.","{player}, la organizaci\xC3\xB3n de {title} abre las puertas a unos pocos en esta ventana, y tu nombre est\xC3\xA1 en la lista.\n\nEs una cita \xC3\xBAnica, que se corre por el prestigio y una bolsa de {reward}.\n\nLa invitaci\xC3\xB3n es tuya. Nos encantar\xC3\xAD""a verte en la parrilla."},
  {"invite_event_v1","There's a special event on the horizon, {player}: {title}.\n\nInvitations are limited, and yours has just been issued: a single date, off the championship, with {reward} on the line for those who show up and deliver.\n\nConsider this your welcome. The rest is up to you.","Hay un evento especial en el horizonte, {player}: {title}.\n\nLas invitaciones son limitadas y la tuya acaba de salir: una sola fecha, fuera del campeonato, con {reward} en juego para quien se presente y responda.\n\nCon esto te damos la bienvenida. Lo dem\xC3\xA1s depende de ti."},
  {"invite_event","{player}, the organisers of {title} are opening the gates to a select few this window, and your name is on the list.\n\nIt's a one-off, run for the prestige and a purse worth {reward}.\n\nThe invitation is yours to take. We'd be glad to see you on the grid.","{player}, la organizaci\xC3\xB3n de {title} abre las puertas a unos pocos en esta ventana, y tu nombre est\xC3\xA1 en la lista.\n\nEs una cita \xC3\xBAnica, que se corre por el prestigio y una bolsa de {reward}.\n\nLa invitaci\xC3\xB3n es tuya. Nos encantar\xC3\xAD""a verte en la parrilla."},
  {"invite_event_res_ignored_v0","You let this one pass, {player}.\n\nNothing rides on it: the invitation simply closes, and the calendar keeps turning.","Lo dejas pasar, {player}.\n\nNo pasa nada: la invitaci\xC3\xB3n simplemente se cierra, y el calendario sigue girando."},
  {"invite_event_res_ignored","You let this one pass, {player}.\n\nNothing rides on it: the invitation simply closes, and the calendar keeps turning.","Lo dejas pasar, {player}.\n\nNo pasa nada: la invitaci\xC3\xB3n simplemente se cierra, y el calendario sigue girando."},
  // invite_sub ({player}{team}{series}{track}{fee}): the regular is NEVER named, no injury is ever invented --
  {"invite_sub_subj","A seat for one round: {track}","Un asiento por una ronda: {track}"},
  {"invite_sub_v0","{player}, this is the team principal at {team}, and I'll be direct because the clock is against us.\n\nOur regular driver has had to step down for the round, and we need someone we trust in the {series} car for {track}: one round, nothing more.\n\nThe fee is {fee}, and the car will be prepared and waiting. If you can be there, say the word and it's done.","{player}, soy el jefe de equipo de {team}, y ir\xC3\xA9 al grano porque vamos con el tiempo justo.\n\nNuestro titular ha tenido que caerse de la ronda, y necesitamos a alguien de confianza en el coche de {series} para {track}: una ronda, nada m\xC3\xA1s.\n\nLa tarifa es {fee} y el coche estar\xC3\xA1 preparado y esper\xC3\xA1ndote. Si puedes estar, dilo y queda cerrado."},
  {"invite_sub_v1","{player}, {team} needs a driver and needs one now.\n\nOur regular driver has had to step down for this round, and I'd rather put a capable pair of hands in the {series} car than scramble.\n\nIt's {track}, one round only, {fee} for the drive, outside the championship, no strings beyond the result you bring. Are you in?","{player}, en {team} necesitamos un piloto y lo necesitamos ya.\n\nNuestro titular ha tenido que caerse de esta ronda, y prefiero poner unas manos capaces en el coche de {series} antes que improvisar.\n\nEs {track}, una sola ronda, {fee} por el volante, fuera del campeonato, sin m\xC3\xA1s ataduras que el resultado que traigas. \xC2\xBFTe apuntas?"},
  {"invite_sub","{player}, this is the team principal at {team}, and I'll be direct because the clock is against us.\n\nOur regular driver has had to step down for the round, and we need someone we trust in the {series} car for {track}: one round, nothing more.\n\nThe fee is {fee}, and the car will be prepared and waiting. If you can be there, say the word and it's done.","{player}, soy el jefe de equipo de {team}, y ir\xC3\xA9 al grano porque vamos con el tiempo justo.\n\nNuestro titular ha tenido que caerse de la ronda, y necesitamos a alguien de confianza en el coche de {series} para {track}: una ronda, nada m\xC3\xA1s.\n\nLa tarifa es {fee} y el coche estar\xC3\xA1 preparado y esper\xC3\xA1ndote. Si puedes estar, dilo y queda cerrado."},
  {"invite_sub_res_accept_v0","Done, {player}. The car is yours for the round, and it'll be prepared and waiting for you.\n\nWelcome to the team for the weekend. Let's make it count.","Hecho, {player}. El coche es tuyo para la ronda, y estar\xC3\xA1 preparado y esper\xC3\xA1ndote.\n\nBienvenido al equipo por el fin de semana. Hag\xC3\xA1moslo valer."},
  {"invite_sub_res_accept","Done, {player}. The car is yours for the round, and it'll be prepared and waiting for you.\n\nWelcome to the team for the weekend. Let's make it count.","Hecho, {player}. El coche es tuyo para la ronda, y estar\xC3\xA1 preparado y esper\xC3\xA1ndote.\n\nBienvenido al equipo por el fin de semana. Hag\xC3\xA1moslo valer."},
  {"invite_sub_res_decline_v0","Understood, {player}. No hard feelings. The timing has to be right, and this time it wasn't.\n\nWe'll find another wheel for the round. Perhaps our paths cross again when it suits us both.","Entendido, {player}. Sin rencores. El momento tiene que ser el adecuado, y esta vez no lo era.\n\nBuscaremos otro volante para la ronda. Quiz\xC3\xA1 nuestros caminos se crucen cuando nos venga bien a los dos."},
  {"invite_sub_res_decline","Understood, {player}. No hard feelings. The timing has to be right, and this time it wasn't.\n\nWe'll find another wheel for the round. Perhaps our paths cross again when it suits us both.","Entendido, {player}. Sin rencores. El momento tiene que ser el adecuado, y esta vez no lo era.\n\nBuscaremos otro volante para la ronda. Quiz\xC3\xA1 nuestros caminos se crucen cuando nos venga bien a los dos."},
  // button: the event announcement's single action (parallel to inbox_act_accept/_decline/_negotiate).
  {"inbox_act_ignore","Ignore","Ignorar"},
  // end of season + advance week + break contract ----
  {"eos_title","SEASON COMPLETE","TEMPORADA TERMINADA"},
  {"eos_sub","All your series have finished","Todas tus series han terminado"},
  {"eos_msg_fmt","Season %d complete. Advance to the next season?","Temporada %d terminada. \xC2\xBF""Avanzar a la siguiente?"},
  {"eos_note","Standings are archived, contracts roll over, the market refreshes.","Se archiva el palmar\xC3\xA9s, los contratos avanzan, el mercado se refresca."},
  {"eos_confirm","ADVANCE SEASON","AVANZAR TEMPORADA"},
  {"eos_advanced_fmt","Now season %d","Ahora temporada %d"},
  {"adv_week","ADVANCE WEEK","AVANZAR SEMANA"},
  {"adv_week_sub","Skip ahead \xC2\xB7 offers expire","Salta el tiempo \xC2\xB7 las ofertas caducan"},
  {"contract_active","ACTIVE CONTRACT","CONTRATO ACTIVO"},
  {"break_contract","BREAK CONTRACT","ROMPER CONTRATO"},
  {"break_contract_warn","Breaking your contract costs 10 REP. Continue?","Romper el contrato cuesta 10 REP. \xC2\xBF""Continuar?"},
  {"break_confirm","BREAK \xE2\x88\x92""10 REP","ROMPER \xE2\x88\x92""10 REP"},
  {"week_lc","week","semana"},
  // Profile UI ---
  {"profile_tab","PROFILE","PERFIL"},
  {"prof_title","DRIVER PROFILE","PERFIL DEL PILOTO"},
  {"prof_sub","Your rating and career arc","Tu rating y arco de carrera"},
  {"prof_rating","RATING","RATING"},
  {"prof_world_rank","WORLD RANKING","RANKING MUNDIAL"},
  {"prof_filter_all","All","Todas"},
  {"prof_career_arc","CAREER ARC","ARCO DE CARRERA"},
  {"prof_form","RECENT FORM","FORMA RECIENTE"},
  {"prof_form_sample"," \xC2\xB7 sample"," \xC2\xB7 ejemplo"},
  {"prof_palmares","PALMAR\xC3\x88S","PALMAR\xC3\x89S"},
  {"prof_rivals","RIVALS & NEMESIS","RIVALES Y N\xC3\x89MESIS"},
  {"prof_titles","TITLES","T\xC3\x8DTULOS"},
  {"prof_xp","EXPERIENCE","EXPERIENCIA"},
  {"prof_age","AGE","EDAD"},
  {"prof_years","yrs","a\xC3\xB1os"},
  {"prof_since_s3","since S-3","desde T-3"},
  // Market enrichment UI ---
  {"off_expectation","TEAM EXPECTATION","EXPECTATIVA DEL EQUIPO"},
  {"off_requires_tryout","Requires tryout","Requiere prueba"},
  {"off_calendar_conflict","Calendar conflict","Conflicto de calendario"},
  {"off_why_you","WHY THEY WANT YOU","POR QU\xC3\x89 TE QUIEREN"},
  {"off_form","FORM","FORMA"},
  {"off_relationship","RELATIONSHIP","RELACI\xC3\x93N"},
  {"off_do_tryout","Do tryout","Hacer prueba"},
  {"off_negotiate","Negotiate","Negociar"},
  {"off_retained","RETAINED OFFERS","OFERTAS RETENIDAS"},
  {"off_req_phase","Requires Phase","Requiere Fase"},
  {"off_superlicense","superlicense","superlicencia"},
  // Negotiate UI ---
  {"neg_with","NEGOTIATION WITH","NEGOCIACI\xC3\x93N CON"},
  {"neg_back_market","Market","Mercado"},
  {"neg_your_rating","Your rating","Tu rating"},
  {"neg_relationship","Relationship","Relaci\xC3\x93n"},
  {"neg_team_offer","Team's initial offer","Oferta inicial del equipo"},
  {"neg_starting_point","Starting point","Punto de partida"},
  {"neg_bonus","Bonus","Prima"},
  {"neg_per_race","Per race","Por carrera"},
  {"neg_duration","Duration","Duraci\xC3\xB3n"},
  {"neg_protection","Protection","Protecci\xC3\xB3n"},
  {"neg_none","None","Sin"},
  {"neg_your_demands","YOUR DEMANDS","TUS DEMANDAS"},
  {"neg_salary_cover","Salary \xE2\x86\x94 Coverage","Salario \xE2\x86\x94 Cobertura"},
  {"neg_salary","Salary","Salario"},
  {"neg_cover","Coverage","Cobertura"},
  {"neg_offer_sponsor","Offer a sponsor","Ofrecer un sponsor"},
  {"neg_offer_sponsor_sub","Hand a sponsor to the team to gain margin","Cede un sponsor al equipo para ganar margen"},
  {"neg_none_offered","None","Ninguno"},
  {"neg_offends","Weak sponsor: offends the team","Sponsor flojo: ofende al equipo"},
  {"neg_warn_sponsor","If you miss the objectives, the sponsor goes to the team.","Si fallas los objetivos, el sponsor pasa al equipo."},
  {"neg_margin","NEGOTIATION MARGIN","MARGEN DE NEGOCIACI\xC3\x93N"},
  {"neg_base","Base margin","Margen base"},
  {"neg_strong","You dictate the terms","Dictas t\xC3\xBA las condiciones"},
  {"neg_weak","Take it or leave it","Lo tomas o lo dejas"},
  {"neg_reject","The team would reject","El equipo rechazar\xC3\xAD""a"},
  {"neg_send","Send proposal","Enviar propuesta"},
  {"neg_accept_asis","Accept as-is","Aceptar tal cual"},
  {"neg_walk","Walk away","Retirarse"},
  {"neg_deal_closed","Deal closed","Acuerdo cerrado"},
  // 3 salary packages + over-reach ding + current-contract strip ---
  {"neg_pkg_more_salary","More salary","M\xC3\xA1s salario"},
  {"neg_pkg_neutral","Neutral 50/50","Neutral 50/50"},
  {"neg_pkg_more_cover","More coverage","M\xC3\xA1s cobertura"},
  {"neg_overreach_ding","Over-asking would damage your standing with this team.","Pasarte de exigente da\xC3\xB1""ar\xC3\xAD""a tu relaci\xC3\xB3n con este equipo."},
  {"neg_current_contract","Current contract","Contrato actual"},
  // active-contract strip in hub/garage (Duration/Money/Role/Objective) + no-contract empty caption
  {"con_active","Active contract","Contrato activo"},
  {"con_duration","Duration","Duraci\xC3\xB3n"},
  {"con_money","Money flow","Flujo de dinero"},
  {"con_role","Role","Rol"},
  {"con_objective","Objective","Objetivo"},
  {"con_renew_table","Renewal on the table","Renovación sobre la mesa"},   // 0.4.0 silly season (garage chip)
  {"con_role_lead","Lead driver","Piloto principal"},
  {"con_role_team","Team driver","Piloto de equipo"},
  {"con_money_self","Self-funded","Autofinanciado"},
  {"con_none_title","No active contract","Sin contrato activo"},
  {"con_none_body","Sign with a team in the Market to see your terms here.","Firma con un equipo en el Mercado para ver tus condiciones aqu\xC3\xAD."},
  // garage car-by-contract-type — a works seat FIXES the car (no picker); bought
  // cars are kept as collectibles (no sell action).
  {"car_fixed_by_team","Car fixed by team","Coche fijado por el equipo"},
  {"collectible","Collectible","Coleccionable"},
  // offer/contract PRESENTATION — type labels + "Includes"
  //      checklist + generated team pitch. Bilingual TR() EN-default (the ES text is LONGER). ----
  // the 6 taxonomy TYPE labels (index = ContractTypeIndex 0..5)
  {"ctype_0","Self-funded","Autofinanciado"},
  {"ctype_1","Pay-per-event","Pago por evento"},
  {"ctype_2","Co-pay","Copago"},
  {"ctype_3","Covered / semi-pro","Cubierto / semipro"},
  {"ctype_4","Paid pro","Pro pagado"},
  {"ctype_5","Factory / works","F\xC3\xA1""brica / oficial"},
  {"ctype_kicker","CONTRACT TYPE","TIPO DE CONTRATO"},
  // "Includes" checklist header + per-category labels + covered/you-pay markers
  {"incl_title","Includes","Incluye"},
  {"incl_seat_entry","Seat & entry","Plaza e inscripci\xC3\xB3n"},
  {"incl_consumables","Tyres, fuel & running","Neum\xC3\xA1ticos, combustible y mantenimiento"},
  {"incl_damage","Repair cover","Cobertura de reparaciones"},
  {"incl_travel","Travel","Desplazamientos"},
  {"incl_team_covers","Team covers","Lo cubre el equipo"},
  {"incl_you_pay","You pay","Lo pagas t\xC3\xBA"},
  {"incl_deductible_note","The deductible is always yours.","El deducible siempre es tuyo."},
  // contract-strip coverage summary ("the team covers X / you pay Y")
  {"con_covers","Team covers","El equipo cubre"},
  {"con_youpay","You pay","Pagas t\xC3\xBA"},
  {"con_covers_all","everything","todo"},
  {"con_youpay_all","everything","todo"},
  // generated team PITCH templates, keyed by TYPE index (the screen picks ctype_pitch_<idx>). Narrative,
  // bilingual; "We are team X… we offer…" framing. The screen prefixes the team name.
  {"ctype_pitch_0","you bring the budget and the drive; we open the door. The seat is yours to fund.",
    "t\xC3\xBA pones el presupuesto y las ganas; nosotros te abrimos la puerta. La plaza corre de tu cuenta."},
  {"ctype_pitch_1","race with us event by event. You cover most of the running; strong results pay bonuses.",
    "corre con nosotros prueba a prueba. T\xC3\xBA cubres la mayor parte de los gastos; los buenos resultados pagan bonos."},
  {"ctype_pitch_2","we share the costs. The team carries part of the seat and consumables; you carry the rest.",
    "compartimos los costes. El equipo asume parte de la plaza y los consumibles; t\xC3\xBA asumes el resto."},
  {"ctype_pitch_3","we cover your seat, tyres and travel so you can focus on driving. Bring us results.",
    "te cubrimos la plaza, los neum\xC3\xA1ticos y los desplazamientos para que solo pienses en conducir. Tr\xC3\xA1""enos resultados."},
  {"ctype_pitch_4","a paid seat with full support: salary, covered costs and a works car. We expect podiums.",
    "una plaza pagada con apoyo total: salario, costes cubiertos y un coche oficial. Esperamos podios."},
  {"ctype_pitch_5","a full factory programme: salary, the works car and everything covered. Win for the marque.",
    "un programa de f\xC3\xA1""brica completo: salario, el coche oficial y todo cubierto. Gana para la marca."},
  // a short prestige flavor appended to the pitch (high-prestige teams add gravitas)
  {"ctype_pitch_prestige","One of the most respected names on the grid.","Uno de los nombres m\xC3\xA1s respetados de la parrilla."},
  // capstone of the living-seat-market work: emergent labels + the job-lost mail +
  //      the derived-team fallback id that career_seats.cpp returns (the engine emits the bare id, the overlay
  //      resolves it here via TR — engine/overlay split). Bilingual TR() EN-default. ----
  // chip on an ELO-displacement offer ("you outpace their backmarker — take his seat")
  {"seat_displacement","Displacement \xC2\xB7 take his seat","Desplazamiento \xC2\xB7 ocupa su asiento"},
  // chip on a pay-driver buy-in offer (an above-pace seat you buy into)
  {"seat_paydriver","Pay-driver \xC2\xB7 buy-in","Pay-driver \xC2\xB7 entrada de pago"},
  // label preceding the buy-in amount shown on a pay-driver offer
  {"seat_buyin","Buy-in","Entrada de pago"},
  // the inbox "you were not renewed / replaced" job-security mail (fires when PlayerLosesSeat at rollover)
  // living-mail system: the job-security mail with a full voice
  // (subj+v0..v2; the bare key = v0, a fallback for old saves).
  {"job_lost_subj","They're not renewing","No renuevan"},
  {"job_lost_v0","{team} won't renew, {player}. I won't dress it up. They made a results call and we didn't give them enough reasons.\n\nYour seat goes back on the market, and so do you. I'm already making calls; check the Market.\n\nAtenea","{team} no renueva, {player}. No te lo voy a adornar: han decidido por resultados y no les dimos motivos suficientes.\n\nTu asiento vuelve al mercado, y t\xC3\xBA tambi\xC3\xA9n. Ya estoy haciendo llamadas; revisa el Mercado.\n\nAtenea"},
  {"job_lost_v1","Bad news first: no renewal from {team}.\n\nNow the useful part, {player}: nobody remembers how a driver lost a seat, only how he answered it. There are teams that will want you, and I'll put them in front of you.\n\nAtenea","Primero la mala: {team} no renueva.\n\nAhora lo \xC3\xBAtil, {player}: nadie recuerda c\xC3\xB3mo un piloto perdi\xC3\xB3 un asiento, solo c\xC3\xB3mo respondi\xC3\xB3. Hay equipos que van a quererte, y yo te los pongo delante.\n\nAtenea"},
  {"job_lost_v2","The door at {team} closed, {player}. It happens to better drivers than both of us.\n\nGive me a few days on the phones. Don't give yourself a single one to sulk. The Market is already open.\n\nAtenea","La puerta de {team} se ha cerrado, {player}. Le pasa a pilotos mejores que t\xC3\xBA y que yo.\n\nDame unos d\xC3\xAD""as al tel\xC3\xA9""fono y no te des ninguno para lamentarte. El Mercado ya est\xC3\xA1 abierto.\n\nAtenea"},
  {"job_lost","{team} won't renew, {player}. I won't dress it up. They made a results call and we didn't give them enough reasons.\n\nYour seat goes back on the market, and so do you. I'm already making calls; check the Market.\n\nAtenea","{team} no renueva, {player}. No te lo voy a adornar: han decidido por resultados y no les dimos motivos suficientes.\n\nTu asiento vuelve al mercado, y t\xC3\xBA tambi\xC3\xA9n. Ya estoy haciendo llamadas; revisa el Mercado.\n\nAtenea"},
  // derived-team FALLBACK label (career_seats.cpp returns this bare id for a nameless lower/cohort seat)
  {"seat_privateer_entry","Privateer entry","Privateer (entrada)"},
  // rating-tier badge chrome (shared by tree/garage/vehicle headers) ----
  {"rating_tier","RATING TIER","CATEGOR\xC3\x8D" "A"},   // kicker above the tier name
  // era-memory calendar tag + Legacy access ----
  {"cal_memory","MEMORY","RECUERDO"},                 // chip on a memory-flagged calendar slot
  {"cal_memory_hint","Race it in Legacy","Cor\xC3\xB3rrela en Legado"},
  {"legacy","LEGACY","LEGADO"},                       // Legacy section / tile label
  {"legacy_sub","Relive your bloodline","Revive tu linaje"},
  // Settings screen (Difficulty section; port of screen_settings.jsx) ----
  {"settings","SETTINGS","AJUSTES"},
  {"settings_sub","Configure your career experience","Configura tu experiencia de carrera"},
  {"set_difficulty","DIFFICULTY","DIFICULTAD"},
  {"set_difficulty_sub","Sets the overall challenge","Define el reto global de tu carrera"},
  {"diff_easy","Easy","F\xC3\xA1""cil"},
  {"diff_easy_d","Gentle AI, all assists, no damage. Learn the tracks.","IA suave, todas las asistencias y sin da\xC3\xB1os. Para aprender los trazados."},
  {"diff_medium","Medium","Medio"},
  {"diff_medium_d","Balanced AI and moderate assists. The sweet spot.","IA equilibrada y asistencias moderadas. El punto dulce."},
  {"diff_hard","Hard","Dif\xC3\xAD""cil"},
  {"diff_hard_d","Fast AI, minimal assists, damage on.","IA r\xC3\xA1pida, asistencias m\xC3\xADnimas y da\xC3\xB1os activados."},
  {"diff_extreme","Extreme","Extremo"},
  {"diff_extreme_d","Max AI, no assists, full damage and strict penalties.","IA al m\xC3\xA1ximo, sin asistencias, da\xC3\xB1os completos y sanciones estrictas."},
  {"diff_adaptive","Adaptive","Adaptativo"},
  {"diff_adaptive_d","The AI matches your pace race by race.","La IA ajusta su ritmo al tuyo carrera a carrera."},
  {"set_examples","The sections below are DESIGN EXAMPLES","Las secciones de abajo son EJEMPLOS de dise\xC3\xB1o"},
  {"set_examples_sub","Only Difficulty is final. The rest is a control-type template, not finalized.","Solo Dificultad es definitiva. El resto es una plantilla de controles, a\xC3\xBAn por definir."},
  {"auto","AUTO","AUTO"},
  // inbox rating-context strip (the world reacts to your unified standing) ----
  {"inbox_tier_ctx","They court drivers around your tier","Buscan pilotos de tu categor\xC3\xAD" "a"},
  {"inbox_your_tier","Your tier","Tu categor\xC3\xAD" "a"},
  // Onboarding gate: the measurement race ----
  {"calib_title","Assessment race","Carrera de medici\xC3\xB3n"},
  {"calib_sub","One race to gauge your level. The rest unlocks after.","Una carrera para medir tu nivel. El resto se desbloquea despu\xC3\xA9s."},
  {"calib_cta","Start measuring race","Empezar carrera de medici\xC3\xB3n"},
  // Settings — difficulty ----
  {"settings_tab","SETTINGS","AJUSTES"},
  {"diff_kicker","Difficulty","Dificultad"},
  {"diff_sub","Sets the overall challenge","Define el reto global de tu carrera"},
  {"diff_easy","Easy","F\xC3\xA1" "cil"},
  {"diff_medium","Medium","Medio"},
  {"diff_hard","Hard","Dif\xC3\xAD" "cil"},
  {"diff_extreme","Extreme","Extremo"},
  {"diff_adaptive","Adaptive","Adaptativo"},
  {"diff_easy_d","Gentle AI. Learn the tracks.","IA suave. Para aprender los trazados."},
  {"diff_medium_d","Balanced AI. The sweet spot.","IA equilibrada. El punto dulce."},
  {"diff_hard_d","Fast AI.","IA r\xC3\xA1pida."},
  {"diff_extreme_d","Max AI.","IA al m\xC3\xA1" "ximo."},
  {"diff_adaptive_d","The AI matches your pace race by race.","La IA ajusta su ritmo al tuyo carrera a carrera."},
  {"diff_custom","Fixed","Fija"},
  {"diff_custom_d","You pick the number and every race runs at it.","El n\xC3\xBAmero lo pones t\xC3\xBA y todas las carreras van a ese."},
  {"diff_trim","Manual trim","Ajuste manual"},
  // review: card DISCOUNT lines in Results -- an i18n key, not a raw label in
  // Spanish (previously hardcoded as "Discount <bucketkey>" / "Rental discount"). A card_line can't carry a label
  // and a key at the same time (career_overlay.cpp query.results / career_api_queries.cpp QueryResults).
  {"results_card_disc_entry","Entry discount","Descuento inscripci\xC3\xB3n"},
  {"results_card_disc_tyres","Tyre discount","Descuento neum\xC3\xA1ticos"},
  {"results_card_disc_fuel","Fuel discount","Descuento combustible"},
  {"results_card_disc_travel","Travel discount","Descuento viaje"},
  {"results_card_disc_rent","Rental discount","Descuento alquiler"},
  // time-jump overlay: jump to date / jump to year on the calendar + the end-of-
  // season CTA, all through the same double-confirmation modal (screen_calendar.cpp DrawJumpConfirmModal).
  {"cal_jump_date_lbl","JUMP TO DATE","SALTAR A FECHA"},
  {"cal_jump_go","GO","IR"},
  {"cal_jump_year_fmt","Jump to %d","Saltar al a\xC3\xB1o %d"},
  {"jump_modal_title","Jump forward in time","Saltar en el tiempo"},
  {"jump_modal_dest_lbl","Destination","Destino"},
  {"jump_modal_crosses_year","This crosses into next season.","Esto cruza a la temporada siguiente."},
  {"jump_modal_no_losses","You will not miss any commitments.","No perder\xC3\xA1s ning\xC3\xBAn compromiso."},
  {"jump_modal_noshow_lbl","You will miss these races:","Perder\xC3\xA1s estas carreras:"},
  {"jump_modal_noshow_warn","Each one counts as a no-show: a DNF in the standings, a reputation hit, a contract penalty, and a risk of losing the seat.","Cada una cuenta como no presentado: DNF en la clasificaci\xC3\xB3n, golpe a la reputaci\xC3\xB3n, penalizaci\xC3\xB3n de contrato y riesgo de perder el asiento."},
  {"jump_modal_continue","I understand, continue","Entendido, continuar"},
  {"jump_modal_confirm","Confirm jump","Confirmar salto"},
  {"jump_modal_back","Back","Atr\xC3\xA1s"},
  {"ro_next_event_cta","Go to the next event of this championship","Ir al siguiente evento de este campeonato"},
};
const char* TR(const char* key){
  if (const char* ov = LangOverlayLookup(key)) return ov;   // language data layer (the eight new languages)
  for (auto& e:TBL) if (!strcmp(e.k,key)) return (g_lang==1)? e.es : e.en;
  return key;
}
bool TRHas(const char* key){
  for (auto& e:TBL) if (!strcmp(e.k,key)) return true;
  return false;
}
void ForEachString(void (*cb)(const char*, const char*, const char*, void*), void* ctx){
  for (auto& e:TBL) cb(e.k, e.en, e.es, ctx);
}
