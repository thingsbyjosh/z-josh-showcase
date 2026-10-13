#include "career_offer_meta.h"
#include "theme.h"

namespace market {

const ContractType& ContractTypeInfo(int kind){
    // order = career::bal::CONTRACT_WORKS/CUSTOMER/PAY_DRIVER/TRIAL/CONDITIONAL (0..4).
    static const ContractType T[5] = {
        {"Works (paga el equipo)","Works (team pays)",        Col::green,  "\xE2\x98\x85"},  // works
        {"Customer (autofinanciado)","Customer (self-funded)",Col::cyan,   "\xE2\x97\x86"},  // ◆ customer
        {"Piloto de pago","Pay driver",                       Col::amber,  "$"},             //   pay_driver
        {"Prueba (medio campeonato)","Trial (half season)",   Col::purple, "\xE2\x97\x90"},  // ◐ trial
        {"Desarrollo (con ascenso)","Development (promotion)",Col::accent, "\xE2\x86\x97"},  // ↗ conditional
    };
    if (kind<0) kind=0; if (kind>4) kind=4; return T[kind];
}

const std::vector<NegDemandView>& NegDemands(){
    // demandId = career::NegoDemandId (0..7). The % cost comes from bal::NEGO_DEMAND_COST[demandId].
    // 8 rows. The first 4 (0..3) unchanged; 4..7 are new (inline EN+ES labels, following
    // this table's own convention; see the UI spec's neg_demand_role/_damage/_calendar/_easier).
    static const std::vector<NegDemandView> D = {
        {0,"Asiento garantizado a fin de temporada","Guaranteed seat to season's end",
            "No pueden reemplazarte por resultados.","They can't replace you over results."},
        {1,"Protecci\xC3\xB3n anti-reemplazo","Anti-replacement protection",
            "Sin cl\xC3\xA1usula de piloto de pago a mitad de a\xC3\xB1o.","No mid-season pay-driver clause."},
        {2,"Ampliar duraci\xC3\xB3n a 2 temporadas","Extend duration to 2 seasons",
            "Estabilidad a cambio de margen.","Stability in exchange for leverage."},
        {3,"Aceptar objetivos m\xC3\xA1s exigentes","Accept tougher objectives",
            "Recuperas margen aceptando m\xC3\xA1s presi\xC3\xB3n.","Regain leverage by taking on more pressure."},
        {4,"Rol de piloto principal","Lead-driver role",
            "Estatus de n\xC3\xBAmero uno; el equipo te respalda.","Number-one status; the team backs you."},
        {5,"Da\xC3\xB1os cubiertos por el equipo","Damage covered by the team",
            "Asumen los costes de reparaci\xC3\xB3n que pagar\xC3\xAD""as t\xC3\xBA.","They absorb repair costs you'd otherwise pay."},
        {6,"Libertad de calendario","Calendar freedom",
            "Sin cl\xC3\xA1usula de exclusividad; corre tambi\xC3\xA9n en otras series.","No exclusivity clause; race elsewhere too."},
        {7,"Objetivos m\xC3\xA1s f\xC3\xA1""ciles","Easier objectives",
            "Metas m\xC3\xA1s bajas \xE2\x80\x94 pero cedes margen.","Lower targets \xE2\x80\x94 but you concede leverage."},
    };
    return D;
}

const SponsorTierView& SponsorTierDisplay(int rank){
    static const SponsorTierView T[5] = {
        {"Local","Local",Col::text2}, {"Regional","Regional",Col::green},
        {"Nacional","National",Col::cyan}, {"Internacional","International",Col::amber},
        {"\xC3\x89lite","Elite",Col::accent},
    };
    if(rank<0)rank=0; if(rank>4)rank=4; return T[rank];
}

const std::vector<MySponsorView>& MySponsors(){
    static const std::vector<MySponsorView> S = {
        {"sp_cafe","Caf\xC3\xA9s del Sur",0}, {"sp_veltro","VeltroTech",1}, {"sp_norden","Norden Bank",2},
    };
    return S;
}

} // namespace market
