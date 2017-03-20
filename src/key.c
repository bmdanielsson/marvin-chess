/*
 * Marvin - an UCI/XBoard compatible chess engine
 * Copyright (C) 2015 Martin Danielsson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <stdlib.h>

#include "key.h"
#include "utils.h"
#include "validation.h"
#include "bitboard.h"

/* 64-bit value for each piece/square combination */
static uint64_t piece_values[NPIECES][NSQUARES] = {
    {18445106750571919008ULL, 18446582733263021028ULL,
     18446340729035666702ULL, 18445695384429394760ULL,
     18444001354837933260ULL, 18439564610669820702ULL,
     18427948407756944528ULL, 18397536543186428564ULL,
     18317917152387756846ULL, 18109470844562257656ULL,
     17563751311884431804ULL, 16135039021676453438ULL,
     12394621683730344192ULL, 2602081964394962115ULL,
     13858368274574159177ULL, 2079534724793314074ULL,
     10826979964925400069ULL, 11954661109158236405ULL,
     6590259280249822941ULL, 7816116744476134305ULL,
     16858090944588645383ULL, 5864667954755600502ULL,
     735912915383188828ULL, 14789814860808569084ULL,
     6740204868659824026ULL, 5430799736581024750ULL,
     9552436358195450091ULL, 4779845923781574008ULL,
     4787101426034211384ULL, 9581458350026092848ULL,
     5510771550266846231ULL, 6950775645583594260ULL,
     15341797382121299938ULL, 2181047698170350740ULL,
     9648251117955862327ULL, 8317122913844261840ULL,
     15303036964091085531ULL, 698580507675635933ULL,
     5239609964501913531ULL, 15020330049610890837ULL,
     2927892049796613984ULL, 12210332169125866119ULL,
     15256441064832069335ULL, 15112246943070912027ULL,
     11633797703488328922ULL, 1342321434198704244ULL,
     10840152668454698814ULL, 12731311842264988998ULL,
     8907200112192288919ULL, 13990288502901868697ULL,
     14617163335620998128ULL, 11414538089737392954ULL,
     1179868208918075515ULL, 10571729934060678361ULL,
     12088658200515119658ULL, 7247661921336701352ULL,
     9654408244455639757ULL, 3268818734025755355ULL,
     152209293773114471ULL, 15634553216708228724ULL,
     9858123553673891226ULL, 13939736784827644855ULL,
     13514584722736873896ULL, 8157353986339094501ULL},
    {10957477244870381762ULL, 6268414334048355616ULL,
     7847927106311075135ULL, 17275366976294991544ULL,
     7084847024191186317ULL, 3979093419612879345ULL,
     4852513915608163423ULL, 10578609659068131790ULL,
     8436570983591750602ULL, 14731103300297054608ULL,
     17310156184036354629ULL, 306038449134385149ULL,
     2054622564705612883ULL, 5857829244982472282ULL,
     15519026502098343612ULL, 3805762126778413558ULL,
     14345165279508002248ULL, 2336245577211448190ULL,
     11110476853397447508ULL, 12548360254080491134ULL,
     8087940494620349507ULL, 11715703246892738472ULL,
     8612344495682626232ULL, 14121572252972391578ULL,
     15305708866190666168ULL, 13348810263300158285ULL,
     6293977862885158960ULL, 5533365320992700767ULL,
     10306198781053598700ULL, 6938648276020153636ULL,
     10509665391815991841ULL, 6143684485204126718ULL,
     7921388076681290200ULL, 17620721740477126053ULL,
     8047450346367412343ULL, 6521629290035195168ULL,
     11517437536623075048ULL, 9584181241761860533ULL,
     17235267533403966869ULL, 5228133219620909000ULL,
     16896037526729865317ULL, 8566410553663783624ULL,
     8803436147078718126ULL, 17843817210906682691ULL,
     7834688687258635549ULL, 5660248842279326930ULL,
     9146057852464247128ULL, 3331422641336212306ULL,
     848371407695915517ULL, 17660354983090365093ULL,
     15239447406973309105ULL, 9611243168414996686ULL,
     13594362774937387798ULL, 12725181742173452757ULL,
     6134438390758320746ULL, 5678375425738891653ULL,
     10900849235494800608ULL, 8577589534597568474ULL,
     14832080713039383914ULL, 17471989203181733639ULL,
     690398757676685929ULL, 3046031807338699503ULL,
     8447858000490900742ULL, 3850798120424488673ULL},
    {3104536356487597983ULL, 5462972298074732889ULL,
     13284541865298210601ULL, 15943828564624528419ULL,
     16100361099607264576ULL, 13910510651897835450ULL,
     7184668795193903950ULL, 7643415074198057519ULL,
     15745818423037650778ULL, 2700632728456446945ULL,
     10802985163602851589ULL, 11261498033451685840ULL,
     4534764858747705803ULL, 2342957874647971219ULL,
     2494108778081166087ULL, 5139529791452066690ULL,
     12924561260055820160ULL, 15187409927890800408ULL,
     14191085777468601803ULL, 8939184007471141451ULL,
     12626466253534813487ULL, 10493712675061110785ULL,
     408089046975413649ULL, 9177379203355449171ULL,
     8677304485086471301ULL, 16854776264721178522ULL,
     4993455502172142155ULL, 16572576311142162947ULL,
     7830704628644373090ULL, 6919618234276831551ULL,
     12928392087003335353ULL, 13418733293537804012ULL,
     8881225047462097422ULL, 13224941861733427705ULL,
     12346856455438699488ULL, 5369044779909546758ULL,
     3760439211851550704ULL, 5912192200454253769ULL,
     13976379385148573992ULL, 17570121221796097712ULL,
     1840738145637867268ULL, 6398917952607841883ULL,
     17356177044042235594ULL, 8776044372613942790ULL,
     8972198086616825347ULL, 18140469210570826406ULL,
     8555802078637262347ULL, 7527179020978305243ULL,
     14025654324811834502ULL, 16103201224489088183ULL,
     15837205266356000188ULL, 12961831845610802303ULL,
     4601626873432543170ULL, 843048770391916258ULL,
     16374424843309296868ULL, 11386898956858350217ULL,
     17786272031560739861ULL, 5078428998994700728ULL,
     15896001034770296109ULL, 5716247306933511983ULL,
     1252740881735291327ULL, 16488719407686946318ULL,
     11320171206723695752ULL, 17471874884854842924ULL},
    {4201965309011701946ULL, 13580845775375670974ULL,
     18093989288147182115ULL, 3807633950236744297ULL,
     11775737295758440054ULL, 13072914544289717173ULL,
     8996423590962713422ULL, 13916275569112604213ULL,
     14305659051255482193ULL, 10553957502354374943ULL,
     17356374800549084172ULL, 4621678760463765281ULL,
     14955566882113316857ULL, 3351533751342040294ULL,
     13546020441259700247ULL, 392958769827105633ULL,
     6079841941863517733ULL, 17846647719544214961ULL,
     10566613077940014859ULL, 13853352859017252369ULL,
     12546620748736502571ULL, 5339845994443415435ULL,
     3473159230231088342ULL, 5079551041058998006ULL,
     11765735888583269065ULL, 11770831895790405989ULL,
     5100257725010727972ULL, 3529941274946848197ULL,
     5489727448866225450ULL, 12939321731137647034ULL,
     14881493683722122270ULL, 13258657241956512769ULL,
     6447653313247012838ULL, 6084544693421907917ULL,
     11806061447979347490ULL, 10886895568216704694ULL,
     2408042531997623809ULL, 14784056760971537229ULL,
     5050639616382842882ULL, 368104088109303320ULL,
     14500336049283897926ULL, 6239657929435505878ULL,
     4218799066584210844ULL, 6416739283202047323ULL,
     15031418774431996534ULL, 1784270909787057699ULL,
     8768218692419514354ULL, 6073802425618510962ULL,
     9453107929245148165ULL, 3838857952188224441ULL,
     2063627259176064808ULL, 2352185174376416378ULL,
     4992928259658235812ULL, 12626599600303323763ULL,
     14440368484654402511ULL, 12247923107511923291ULL,
     3856576108980964163ULL, 17768791288777902984ULL,
     12556390286599310842ULL, 1453635505900450083ULL,
     10251340964297428685ULL, 10853643326167186244ULL,
     3863086940426927899ULL, 735698163189332144ULL},
    {16790751618555709199ULL, 12743310557875906014ULL,
     2992355321876638348ULL, 14680660809025132998ULL,
     4156138970664596868ULL, 16234580836164065666ULL,
     7654276739444886950ULL, 6728249373580698158ULL,
     12530471394182109411ULL, 12416662730893441850ULL,
     6272692065302845645ULL, 6401655477832327656ULL,
     12932274359604221514ULL, 13948504208231478194ULL,
     10466655518942215025ULL, 17451381689109348001ULL,
     4994082077632470158ULL, 15977769945059186441ULL,
     6045820291086660077ULL, 2159852260057371003ULL,
     433655821009680679ULL, 17587939940462065172ULL,
     15436837197698834362ULL, 10275827583219909942ULL,
     15390887560483141958ULL, 17450010360739178141ULL,
     65816719056730771ULL, 1194183865845654837ULL,
     3516896214631740684ULL, 9356666114201111723ULL,
     6106277381891322104ULL, 8962408048585054456ULL,
     2334122017783568882ULL, 16486944078407534490ULL,
     10233141410534112479ULL, 14212722166012054300ULL,
     13958442341354089942ULL, 9215780124854845032ULL,
     13689140046027677725ULL, 13404815262852948466ULL,
     8078723013563057595ULL, 10831353786426215256ULL,
     5968755599567608912ULL, 7075074361313057875ULL,
     15256467475781667686ULL, 1801001267649232003ULL,
     8593199728504859170ULL, 5531853839860845380ULL,
     8002603808189895619ULL, 29294174780057257ULL,
     10532022785564916818ULL, 13120272125317341449ULL,
     10382130176163356014ULL, 18026118411762717531ULL,
     6802978924522907139ULL, 2382737689435283119ULL,
     345395479934449163ULL, 17100192819782705036ULL,
     14061856176736004252ULL, 6638792981457235205ULL,
     5854442090969994519ULL, 10924613972413460057ULL,
     8472817080122387609ULL, 14493837276543693708ULL},
    {16562192684321388394ULL, 16745916043225082196ULL,
     15228892031130181807ULL, 10494177321197353147ULL,
     16253720604832598402ULL, 1373496354471329767ULL,
     6313673864147482162ULL, 17567686569827693932ULL,
     9495817038430677525ULL, 10920006558281571214ULL,
     4817458558409554771ULL, 3532530448803613967ULL,
     5780052132810435545ULL, 13807706609113567895ULL,
     17196484965562139279ULL, 888260148743737650ULL,
     3915200886235164934ULL, 10857503846113301660ULL,
     10210485906024448883ULL, 1327209811135414044ULL,
     12218129596728708253ULL, 16880354245855321438ULL,
     1529525670083878459ULL, 6154966833810898258ULL,
     16935616831281147000ULL, 7758395525498360182ULL,
     6339569736623998956ULL, 11260555701485855335ULL,
     8995514621685606570ULL, 15725907504085126713ULL,
     1288800419816414749ULL, 6587399160930190015ULL,
     26572321188868992ULL, 11939223208202508225ULL,
     17344353238299095006ULL, 3200509704017096317ULL,
     10703919943166834610ULL, 10464667383630376766ULL,
     2243257478823911271ULL, 14712010454112499797ULL,
     4999447085130931286ULL, 286250128909554512ULL,
     14306128039088107606ULL, 5738726521896339218ULL,
     2910051518010975458ULL, 2991670049248805805ULL,
     6065119961592019169ULL, 15203770499308037879ULL,
     2652864737949437634ULL, 11201487111584119793ULL,
     12504852535978272017ULL, 7866407082127019871ULL,
     11094530059439177645ULL, 6970358345815292169ULL,
     9816786995118898729ULL, 4033177893461112854ULL,
     2282746680969491321ULL, 2815304166559560976ULL,
     6163165814414261875ULL, 15674354608539745516ULL,
     3966329208595019859ULL, 14671457750440722121ULL,
     3154636576268736198ULL, 13239357379636591659ULL},
    {18116691497521459320ULL, 4217228974098617663ULL,
     12981981494121308673ULL, 16281890775069919079ULL,
     17417027429749655281ULL, 17522447449059429740ULL,
     16703812839356464496ULL, 14142327671966100198ULL,
     7276587443278796288ULL, 7687353998384451003ULL,
     15785555211360431948ULL, 2775984837314131661ULL,
     10989143365701636405ULL, 11744862535117634770ULL,
     5798861497798293503ULL, 5651641285906524973ULL,
     11156304377033481283ULL, 9370447094818679199ULL,
     16955278920239807668ULL, 4601982195147309857ULL,
     15297411730321795273ULL, 4396926197435362782ULL,
     16340110927103966443ULL, 7730079785493823367ULL,
     6850047752711834378ULL, 12820305485458912339ULL,
     13164124638545323179ULL, 8225485684029059155ULL,
     11512251758351021483ULL, 7864686844876044815ULL,
     12081970125313559357ULL, 9934398780689374797ULL,
     17721306893420309444ULL, 6336195096893873059ULL,
     1287278392966398784ULL, 15972626155647186811ULL,
     9737031267070239540ULL, 13238628990304973344ULL,
     11532111621545250633ULL, 2911123149657635772ULL,
     15648163228698818215ULL, 7139878401904636313ULL,
     5771471968425156134ULL, 10174779520483031956ULL,
     6306041842648700057ULL, 8743507356499495828ULL,
     1477816816921021989ULL, 14136687163678210805ULL,
     4038998543806707064ULL, 16427213869013071919ULL,
     8349235592479093528ULL, 8620654253165687764ULL,
     17512646490352262919ULL, 7023797083356899651ULL,
     3558825419204311262ULL, 3652921191368215220ULL,
     7399857482529613630ULL, 100149186738372036ULL,
     11347253479024333326ULL, 15495028521366536645ULL,
     16691249356107204093ULL, 16131894796579835957ULL,
     13257932972739984736ULL, 5195079388444747756ULL},
    {2327547192526589217ULL, 1787481516764299129ULL,
     3035058706802717001ULL, 7317694599348940924ULL,
     471442353686042319ULL, 12543376531123826699ULL,
     265441109378534416ULL, 6699771534502095558ULL,
     1387129420418238208ULL, 15908360796167203386ULL,
     9444706833481520075ULL, 12425840380943026120ULL,
     9386070227048109644ULL, 15732450976867028440ULL,
     917955900875295200ULL, 5468080127097706789ULL,
     15486526480350155852ULL, 4097930511342805953ULL,
     15254251123025195793ULL, 4771496059350105810ULL,
     17506900452068966407ULL, 10855797826103415810ULL,
     15060573698612039355ULL, 15879340540764573393ULL,
     14130623173306441147ULL, 8065784914035133026ULL,
     10066812245464702340ULL, 3688149748581734282ULL,
     997556332204747035ULL, 17751505321674407905ULL,
     15363552162065042733ULL, 9892407095106173540ULL,
     14313669127548445183ULL, 14602098226646861747ULL,
     11045962138168388543ULL, 89044127033691719ULL,
     7667914312347270933ULL, 4468196736230918933ULL,
     5736756577306122443ULL, 12742072987097551369ULL,
     14042798991237672972ULL, 10939741240467469504ULL,
     329600001264332341ULL, 8495883500815902874ULL,
     6711306423178857372ULL, 11638197117757096855ULL,
     9756702183944491496ULL, 17631828774590539971ULL,
     6245376669073769597ULL, 1104462564487289687ULL,
     15514755093802740130ULL, 8546475914243269010ULL,
     10124833993668545999ULL, 3381201320682096605ULL,
     19011972605060579ULL, 15122659334623422924ULL,
     8455639228587584064ULL, 10244177691653491605ULL,
     3830149768368390623ULL, 1246432949603187209ULL,
     18355893149855811670ULL, 16928000365362377144ULL,
     13981525212968261170ULL, 6569750540347035871ULL},
    {5727887735634418797ULL, 10613912679441178753ULL,
     7667348224616929237ULL, 12388212675370245535ULL,
     11050545719194377509ULL, 2316922425615516463ULL,
     14346884954696035432ULL, 3830486308165705253ULL,
     15591398702996432041ULL, 6050221666289427092ULL,
     2559266291576881940ULL, 1627819208373549414ULL,
     2324110678352914717ULL, 5344674158541734385ULL,
     13709911792977358707ULL, 17338397823346515749ULL,
     1411954874384508064ULL, 5344130201145839290ULL,
     14620516392833852329ULL, 1624172847048795553ULL,
     8698746217727156213ULL, 6025483064279642339ULL,
     9377622319920919219ULL, 3660720485554387444ULL,
     1604539136742243114ULL, 1153058256528900330ULL,
     1854796981880904271ULL, 4411252016743072933ULL,
     11379039736424105564ULL, 11279123114524724850ULL,
     4011827550552717239ULL, 756278864762706100ULL,
     16703995117377283361ULL, 12462218348540012910ULL,
     2236077199274645290ULL, 12692837982479312238ULL,
     17395854019195218909ULL, 2601155268201403597ULL,
     8854436522899386019ULL, 5515652226719514749ULL,
     7692439502068306643ULL, 17561827607046996315ULL,
     8099635852614253214ULL, 6737160610281582209ULL,
     12111845991115451646ULL, 11151875284992584504ULL,
     2897197139189196647ULL, 15986540865770357151ULL,
     8168937319292762515ULL, 8520513104925162965ULL,
     17392682654968526479ULL, 6763966057370480440ULL,
     2899457517075245526ULL, 1934325821484535372ULL,
     2903761964490579239ULL, 6777040735767988521ULL,
     17427440906594172501ULL, 8611793849480383986ULL,
     8408182637484324065ULL, 16612754071562560365ULL,
     4536752774525676554ULL, 15444248317134142667ULL,
     4902665378494057049ULL, 17710572551543398976ULL},
    {11335564137307027588ULL, 16296361868899949064ULL,
     660194666715176692ULL, 4130966200660202895ULL,
     11732865267121952860ULL, 12620885539881062303ULL,
     7683289274449027042ULL, 10428901628275167238ULL,
     5156913536599272524ULL, 5042000313379208766ULL,
     9969006748347502189ULL, 6418437185515337322ULL,
     9286466157234956172ULL, 2994136540109240030ULL,
     18142768200583158056ULL, 14540841258962553663ULL,
     7033011511184923474ULL, 6558193266002282169ULL,
     12641729631563364569ULL, 12920251563568250860ULL,
     7672522981069181004ULL, 10097236724448440567ULL,
     4172685118498938549ULL, 2420979962904933512ULL,
     3090174115025010402ULL, 6849703714026656124ULL,
     17459098358911535183ULL, 8634022560097994611ULL,
     8443211317019812040ULL, 16695530731475603847ULL,
     4749973406653640681ULL, 16001294889756423382ULL,
     6360423128081484469ULL, 3080135826344550892ULL,
     2879903678581447441ULL, 5559655890360503136ULL,
     13799225320061634320ULL, 17391356672780555055ULL,
     1481517895602406715ULL, 5499860415365477155ULL,
     15018063346199076237ULL, 2661083492924848194ULL,
     11411850529619313115ULL, 13127724035108460205ULL,
     9524738829558107021ULL, 15446653798307339958ULL,
     18368397827873556280ULL, 2765051546484160244ULL,
     8373581549069318589ULL, 3909110363165750853ULL,
     3353910872284511184ULL, 6152541598496931114ULL,
     15103955918843664329ULL, 2265918691575595221ULL,
     10140544225297761999ULL, 9709211910540469847ULL,
     540428113574732505ULL, 10358816495303382256ULL,
     12089357979586555571ULL, 7462674697308286414ULL,
     10298666125223243122ULL, 4986660268432696296ULL,
     4661314675779878472ULL, 8997525776019157769ULL},
    {3884679910424601651ULL, 2656433282883926418ULL,
     4084861955339377470ULL, 9598071910763485224ULL,
     6262851703173876055ULL, 9190644547794570554ULL,
     2862257194129563225ULL, 17843032440160210385ULL,
     13773351987521936857ULL, 5030279457285983163ULL,
     1317728384268362099ULL, 17369730433009422144ULL,
     13897894107855001006ULL, 5877449829663261831ULL,
     3734374704469096425ULL, 5325916300856246093ULL,
     12243454857585460735ULL, 12957865547227030892ULL,
     8183317033720392264ULL, 11592327571046345767ULL,
     8146840929043386578ULL, 12848195228968734636ULL,
     11951242679790629105ULL, 4558708081502749480ULL,
     1725042896574158984ULL, 616420608219765037ULL,
     124299596160908382ULL, 18203222249677544429ULL,
     17592121018269873030ULL, 16126477403793225033ULL,
     12340567123695255315ULL, 2448479902172923889ULL,
     13451858652170431356ULL, 1013527251728415365ULL,
     8035709176656697038ULL, 4646775532161403368ULL,
     5904778768863921897ULL, 13067560765840484078ULL,
     14851320803984368771ULL, 13039657563813192376ULL,
     5821149830857837828ULL, 4423711252094633046ULL,
     7450225942538261177ULL, 17926885898854443640ULL,
     9437024283271692142ULL, 10384186959550567377ULL,
     3269034521602807841ULL, 17869822010823966192ULL,
     13446943372039959662ULL, 4024425376327783933ULL,
     17073076822063046725ULL, 10301397619107978641ULL,
     13831277380002311951ULL, 12745609770523717535ULL,
     5958888538820000745ULL, 5131217173497857054ULL,
     9434924330710016812ULL, 4726731068256953705ULL,
     4745430223097253134ULL, 9509559596739894747ULL,
     5336746493345191396ULL, 6500599228104827856ULL,
     14165212518530883307ULL, 17548455598519749550ULL},
    {1586585470123424451ULL, 5658044881265108122ULL,
     15387629837452742438ULL, 3611517832710406012ULL,
     13893667725798148968ULL, 1176239214377118748ULL,
     8081793986747829159ULL, 4622560004013337982ULL,
     5785805370101333202ULL, 12734936765776536851ULL,
     13972422202555115785ULL, 10735505091513589609ULL,
     18234254416727094578ULL, 7073770019838581833ULL,
     2987216974645171788ULL, 1887800231726231547ULL,
     2676425737645722720ULL, 6141476976916006881ULL,
     15748166524958818790ULL, 4209453795350513457ULL,
     15327180930439636585ULL, 4878600861434213738ULL,
     17755365718982621653ULL, 11494250160911799346ULL,
     16727465436123478371ULL, 1794577340553732439ULL,
     7103252659179620027ULL, 1068517231351310718ULL,
     14549043104288934011ULL, 5685123946981289973ULL,
     2506490068511475557ULL, 1834346254258225750ULL,
     2996790711375382778ULL, 7155945207497201816ULL,
     24462173558178000ULL, 11364185382591972850ULL,
     15621511245249611689ULL, 17053604283742315462ULL,
     17092557540857717673ULL, 15777566260758668114ULL,
     11793477844374404337ULL, 1156123207244984220ULL,
     10121797178631653509ULL, 10762524267825382925ULL,
     3719273551067274336ULL, 395377053452174774ULL,
     15913521010628099616ULL, 10451859175754481163ULL,
     15442217857081855677ULL, 17427969658000729295ULL,
     18394947047505766671ULL, 863544681838927806ULL,
     2642592403577145574ULL, 7064151860816736662ULL,
     103199773239303834ULL, 11692191528315759160ULL,
     16526872750815673385ULL, 995019253377827047ULL,
     4904929078732429639ULL, 13719767978524494575ULL,
     17807872795948753825ULL, 2810442938568332952ULL,
     9070200089170885696ULL, 5953655255167103207ULL}
};

/* 64-bit value for each possible en passant target square */
static uint64_t ep_values[NSQUARES] = {
    8790846357291060502ULL, 1972059070625824699ULL,
    15572316928228295895ULL, 7851322907154140877ULL,
    7981893806051359307ULL, 16094358502410040017ULL,
    3407854902796047564ULL, 12575869603021985009ULL,
    15873171177301816166ULL, 16597061199915390973ULL,
    15471187672069098294ULL, 11369757751172305669ULL,
    191583520555499670ULL, 7651656211833024188ULL,
    4316883041166351965ULL, 5298992924550971158ULL,
    11580257064343082375ULL, 10995034190473794622ULL,
    2958182114329424018ULL, 16326256217634094456ULL,
    9127340403971007475ULL, 11055926339020407068ULL,
    5593855866942253250ULL, 5725560606615482315ULL,
    11582906616685036217ULL, 10576576501586595589ULL,
    1699998159174366133ULL, 12970404045283417814ULL,
    317645174065913713ULL, 6429356214404717462ULL,
    523840731590194003ULL, 13589071385931993375ULL,
    3349804623595812526ULL, 14907086549975080009ULL,
    4478128227946733103ULL, 16974042198984773888ULL,
    9550590898254210960ULL, 11677891840519281745ULL,
    7036259872928413380ULL, 9431129795378158262ULL,
    2810385435201580060ULL, 17446931915792654400ULL,
    12636922173347252067ULL, 2017090539129484778ULL,
    11861335513388117271ULL, 15120091272134463835ULL,
    15052355556867294973ULL, 11590231333347860408ULL,
    1271836382283967208ULL, 10672102546699392930ULL,
    12297727196989599418ULL, 7774415630045710155ULL,
    11025681042183921096ULL, 6855802746130832238ULL,
    9541888545245003231ULL, 3323280147751203053ULL,
    427871229932852457ULL, 16407238947613445582ULL,
    11900357474078353216ULL, 847170077577788079ULL,
    9088058164221083502ULL, 7970260337080999864ULL,
    14822884191763357626ULL, 18051809504946033203ULL
};

/* 64-bit value for each color */
static uint64_t color_values[NSIDES] = {
    2438975516169819873ULL, 7712103117205327497ULL
};

/* 64-bit value for all possible combination of castling flags */
static uint64_t castle_values[16] = {
    2250670429812345694ULL, 17486652241646331469ULL,
    13315878824373271112ULL, 4014401502505353006ULL,
    17174069748262442494ULL, 10614319603452805839ULL,
    14669131070618259081ULL, 14946490879433880106ULL,
    11723597485383932596ULL, 1777718852044756117ULL,
    12056303135870009125ULL, 15944607826597161179ULL,
    17330856942582662347ULL, 17601380272182753346ULL,
    17026459123590339232ULL, 15031333701544438364ULL
};

uint64_t key_generate(struct gamestate *pos)
{
    uint64_t key;
    int      k;

    assert(valid_board(pos));

    /* Add pieces */
    key = 0ULL;
    for (k=0;k<NSQUARES;k++) {
        if (pos->pieces[k] == NO_PIECE) {
            continue;
        }
        key ^= piece_values[pos->pieces[k]][k];
    }

    /* Add en-passant target square */
    if (pos->ep_sq != NO_SQUARE) {
        key ^= ep_values[pos->ep_sq];
    }

    /* Add castling permissions */
    key ^= castle_values[pos->castle];

    /* Add side to move */
    key ^= color_values[pos->stm];

    return key;
}

uint64_t key_generate_pawnkey(struct gamestate *pos)
{
    uint64_t pawns;
    uint64_t key;
    int      sq;

    assert(valid_board(pos));

    key = 0ULL;
    pawns = pos->bb_pieces[WHITE_PAWN];
    while (pawns != 0ULL) {
        sq = POPBIT(&pawns);
        key ^= piece_values[WHITE_PAWN][sq];
    }
    pawns = pos->bb_pieces[BLACK_PAWN];
    while (pawns != 0ULL) {
        sq = POPBIT(&pawns);
        key ^= piece_values[BLACK_PAWN][sq];
    }

    return key;
}

uint64_t key_update_piece(uint64_t key, int piece, int sq)
{
    key ^= piece_values[piece][sq];
    return key;
}

uint64_t key_update_ep_square(uint64_t key, int old_sq, int new_sq)
{
    if (old_sq != NO_SQUARE) {
        key ^= ep_values[old_sq];
    }
    if (new_sq != NO_SQUARE) {
        key ^= ep_values[new_sq];
    }
    return key;
}

uint64_t key_update_side(uint64_t key, int new_color)
{
    key ^= color_values[FLIP_COLOR(new_color)];
    key ^= color_values[new_color];
    return key;
}

uint64_t key_update_castling(uint64_t key, int old_castle, int new_castle)
{
    key ^= castle_values[old_castle];
    key ^= castle_values[new_castle];
    return key;
}
