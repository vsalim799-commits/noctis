#include "Noctis/Research/Hypothesis.h"

#include "Noctis/Core/Random.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace noctis
{
const char* measureLabelFr(Measure m)
{
    switch (m)
    {
    case Measure::GroupSpacing: return "espacement moyen au plus proche voisin (m)";
    case Measure::VigilantFraction: return "proportion d'individus vigilants";
    case Measure::FeedingFraction: return "proportion d'individus en alimentation";
    case Measure::GroupSize: return "taille de groupe observée";
    case Measure::CallRatePerMin: return "vocalisations détectées par minute";
    case Measure::ObserverDistance: return "distance de l'observateur (m)";
    case Measure::DroneAltitude: return "altitude du drone (m)";
    case Measure::Temperature: return "température de l'air (°C)";
    case Measure::Light: return "éclairement (log10 lux)";
    case Measure::TimeOfDay: return "heure de la journée";
    case Measure::Count: break;
    }
    return "?";
}

const char* measureKey(Measure m)
{
    static const char* const keys[] = {"group_spacing", "vigilant_fraction", "feeding_fraction", "group_size", "call_rate",
                                       "observer_distance", "drone_altitude", "temperature", "light", "time_of_day"};
    const int i = static_cast<int>(m);
    return i >= 0 && i < static_cast<int>(Measure::Count) ? keys[i] : "?";
}

int parseMeasure(const std::string& key)
{
    for (int i = 0; i < static_cast<int>(Measure::Count); ++i)
    {
        if (key == measureKey(static_cast<Measure>(i)))
        {
            return i;
        }
    }
    return -1;
}

const char* conditionLabelFr(ConditionKind c)
{
    switch (c)
    {
    case ConditionKind::DroneAirborne: return "drone en vol";
    case ConditionKind::VehicleEngineOn: return "moteur du véhicule en marche";
    case ConditionKind::PredatorVisible: return "prédateur visible";
    case ConditionKind::Night: return "nuit";
    case ConditionKind::ObserverWithin: return "observateur à moins de X m";
    case ConditionKind::TimeBetween: return "plage horaire";
    case ConditionKind::MeasureAbove: return "mesure au-dessus d'un seuil";
    }
    return "?";
}

const char* hypothesisStatusLabelFr(HypothesisStatus s)
{
    switch (s)
    {
    case HypothesisStatus::Open: return "Ouverte — données insuffisantes";
    case HypothesisStatus::Supported: return "Soutenue par les données";
    case HypothesisStatus::NotSupported: return "Non soutenue par les données";
    case HypothesisStatus::Inconclusive: return "Non concluante";
    }
    return "?";
}

double HypothesisSystem::measureOf(const SessionSample& s, Measure m, const std::vector<AcousticDetection>& det, u32 sessionId)
{
    switch (m)
    {
    case Measure::GroupSpacing: return s.meanSpacingM;
    case Measure::VigilantFraction: return s.vigilantFraction;
    case Measure::FeedingFraction: return s.feedingFraction;
    case Measure::GroupSize: return s.count;
    case Measure::CallRatePerMin:
    {
        int n = 0;
        for (const AcousticDetection& d : det)
        {
            if (d.sessionId == sessionId && std::fabs(d.time - s.time) <= 30.0)
            {
                ++n;
            }
        }
        return static_cast<double>(n);
    }
    case Measure::ObserverDistance: return s.observerDistanceM;
    case Measure::DroneAltitude: return s.droneAirborne ? s.droneAltitudeM : 0.0;
    case Measure::Temperature: return s.temperatureC;
    case Measure::Light: return std::log10(std::max(1e-4f, s.lux));
    case Measure::TimeOfDay: return s.hourOfDay;
    case Measure::Count: break;
    }
    return 0.0;
}

bool HypothesisSystem::conditionHolds(const SessionSample& s, const Condition& c, const std::vector<AcousticDetection>& det, u32 sessionId)
{
    switch (c.kind)
    {
    case ConditionKind::DroneAirborne: return s.droneAirborne;
    case ConditionKind::VehicleEngineOn: return s.vehicleEngineOn;
    case ConditionKind::PredatorVisible: return s.predatorVisible;
    case ConditionKind::Night: return s.lux < 10.0f;
    case ConditionKind::ObserverWithin: return s.observerDistanceM < c.value;
    case ConditionKind::TimeBetween: return s.hourOfDay >= c.value && s.hourOfDay <= c.value2;
    case ConditionKind::MeasureAbove: return measureOf(s, c.measure, det, sessionId) > c.value;
    }
    return false;
}

u32 HypothesisSystem::create(Hypothesis h, double now)
{
    h.id = nextId_++;
    h.created = now;
    h.status = HypothesisStatus::Open;
    hypotheses_.push_back(h);
    return h.id;
}

const Hypothesis* HypothesisSystem::find(u32 id) const
{
    for (const Hypothesis& h : hypotheses_)
    {
        if (h.id == id)
        {
            return &h;
        }
    }
    return nullptr;
}

Hypothesis* HypothesisSystem::findMutable(u32 id)
{
    for (Hypothesis& h : hypotheses_)
    {
        if (h.id == id)
        {
            return &h;
        }
    }
    return nullptr;
}

namespace hypimpl
{
struct Tagged
{
    u32 session = 0;
    SessionSample s;
};

std::string fmt(const char* f, double a, double b = 0.0, double c = 0.0, double d = 0.0)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), f, a, b, c, d);
    return buf;
}
} // namespace hypimpl

void HypothesisSystem::test(u32 id, const ObservationSystem& obs, const std::vector<AcousticDetection>& detections, double now, u64 seed, u32 expeditionId)
{
    using namespace hypimpl;
    Hypothesis* h = findMutable(id);
    if (!h)
    {
        return;
    }
    h->warningsFr.clear();
    h->lastTested = now;
    ++h->tests;
    // Collect samples, thinned to >= 30 s apart within a session to limit autocorrelation.
    std::vector<Tagged> samples;
    std::set<u32> sessionsUsed;
    for (const ObservationSession& s : obs.sessions())
    {
        if (expeditionId != 0 && s.expeditionId != expeditionId)
        {
            continue;
        }
        double last = -1e9;
        for (const SessionSample& smp : s.samples)
        {
            if (h->speciesIndex >= 0 && smp.speciesIndex != h->speciesIndex)
            {
                continue;
            }
            if (smp.time - last < 30.0)
            {
                continue;
            }
            last = smp.time;
            samples.push_back({s.id, smp});
            sessionsUsed.insert(s.id);
        }
    }
    h->methodFr = "Échantillons issus de " + std::to_string(sessionsUsed.size()) + " session(s) d'observation, espacés d'au moins 30 s pour limiter l'autocorrélation. ";
    if (sessionsUsed.size() < 3)
    {
        h->warningsFr.push_back("Moins de trois sessions indépendantes : risque de pseudo-réplication (mêmes individus, mêmes conditions).");
    }
    std::vector<double> a;
    std::vector<double> b;
    bool directional = true;
    int expectedSign = 0;
    switch (h->predicted)
    {
    case PredictedDirection::Increase:
    case PredictedDirection::Positive: expectedSign = 1; break;
    case PredictedDirection::Decrease:
    case PredictedDirection::Negative: expectedSign = -1; break;
    default: directional = false; break;
    }

    if (h->kind == HypothesisKind::Comparison || h->kind == HypothesisKind::Association)
    {
        std::vector<double> distA;
        std::vector<double> distB;
        int droneA = 0;
        int droneB = 0;
        int nightA = 0;
        int nightB = 0;
        for (const Tagged& t : samples)
        {
            const bool cond = conditionHolds(t.s, h->condition, detections, t.session);
            const double v = measureOf(t.s, h->response, detections, t.session);
            (cond ? a : b).push_back(v);
            (cond ? distA : distB).push_back(t.s.observerDistanceM);
            (cond ? droneA : droneB) += t.s.droneAirborne ? 1 : 0;
            (cond ? nightA : nightB) += t.s.lux < 10.0f ? 1 : 0;
        }
        if (h->kind == HypothesisKind::Comparison)
        {
            h->result = mannWhitney(a, b, seed);
            h->methodFr += "Comparaison de la mesure « " + std::string(measureLabelFr(h->response)) + " » entre les échantillons où la condition « " +
                           conditionLabelFr(h->condition.kind) + " » est vraie (A) et fausse (B) : test de Mann–Whitney, p par permutation, IC 95 % par bootstrap.";
        }
        else
        {
            // Association: condition (A) vs response above the overall median.
            std::vector<double> all(a);
            all.insert(all.end(), b.begin(), b.end());
            const double med = median(all);
            int aa = 0;
            int ab = 0;
            int ba = 0;
            int bb = 0;
            for (const double v : a)
            {
                (v > med ? aa : ab)++;
            }
            for (const double v : b)
            {
                (v > med ? ba : bb)++;
            }
            h->result = fisherExact(aa, ab, ba, bb);
            h->result.medianA = a.empty() ? 0.0 : static_cast<double>(aa) / static_cast<double>(a.size());
            h->result.medianB = b.empty() ? 0.0 : static_cast<double>(ba) / static_cast<double>(b.size());
            h->methodFr += "Tableau de contingence (condition × mesure au-dessus de la médiane) : test exact de Fisher.";
        }
        // Confounds.
        if (h->response != Measure::ObserverDistance && !distA.empty() && !distB.empty())
        {
            const double mA = median(distA);
            const double mB = median(distB);
            if (std::fabs(mA - mB) > 0.3 * std::max(1.0, std::max(mA, mB)))
            {
                h->warningsFr.push_back(fmt("Effet observateur possible : distance médiane d'observation %.0f m (A) contre %.0f m (B).", mA, mB));
            }
        }
        if (h->condition.kind != ConditionKind::DroneAirborne && !a.empty() && !b.empty())
        {
            const double fa = static_cast<double>(droneA) / static_cast<double>(a.size());
            const double fb = static_cast<double>(droneB) / static_cast<double>(b.size());
            if (std::fabs(fa - fb) > 0.3)
            {
                h->warningsFr.push_back(fmt("Présence du drone déséquilibrée entre conditions (%.0f %% contre %.0f %%) : facteur de confusion possible.", fa * 100.0,
                                            fb * 100.0));
            }
        }
        if (h->condition.kind != ConditionKind::Night && h->condition.kind != ConditionKind::TimeBetween && !a.empty() && !b.empty())
        {
            const double fa = static_cast<double>(nightA) / static_cast<double>(a.size());
            const double fb = static_cast<double>(nightB) / static_cast<double>(b.size());
            if (std::fabs(fa - fb) > 0.3)
            {
                h->warningsFr.push_back("Répartition jour/nuit différente entre conditions : le rythme d'activité peut expliquer l'effet.");
            }
        }
    }
    else if (h->kind == HypothesisKind::EventResponse)
    {
        // Event-triggered comparison of (after - before) against control windows without events.
        std::vector<double> eventTimes;
        std::vector<u32> eventSessions;
        for (const ObservationSession& s : obs.sessions())
        {
            if (sessionsUsed.count(s.id) == 0)
            {
                continue;
            }
            if (h->trigger == EventTrigger::CallDetected || h->trigger == EventTrigger::CallCluster)
            {
                for (const AcousticDetection& d : detections)
                {
                    if (d.sessionId == s.id && (h->trigger == EventTrigger::CallDetected || d.cluster == h->callCluster))
                    {
                        eventTimes.push_back(d.time);
                        eventSessions.push_back(s.id);
                    }
                }
            }
            else
            {
                bool prev = false;
                for (const SessionSample& smp : s.samples)
                {
                    const bool now2 = h->trigger == EventTrigger::PredatorAppears ? smp.predatorVisible : (smp.droneAirborne && smp.droneAltitudeM < 30.0f);
                    if (now2 && !prev)
                    {
                        eventTimes.push_back(smp.time);
                        eventSessions.push_back(s.id);
                    }
                    prev = now2;
                }
            }
        }
        auto deltaAt = [&](u32 session, double t, double& out) {
            double sb = 0.0;
            double sa = 0.0;
            int nb = 0;
            int na = 0;
            for (const Tagged& x : samples)
            {
                if (x.session != session)
                {
                    continue;
                }
                const double v = measureOf(x.s, h->response, detections, session);
                if (x.s.time >= t - h->windowS && x.s.time < t)
                {
                    sb += v;
                    ++nb;
                }
                else if (x.s.time >= t && x.s.time <= t + h->windowS)
                {
                    sa += v;
                    ++na;
                }
            }
            if (nb == 0 || na == 0)
            {
                return false;
            }
            out = sa / na - sb / nb;
            return true;
        };
        for (size_t i = 0; i < eventTimes.size(); ++i)
        {
            double d = 0.0;
            if (deltaAt(eventSessions[i], eventTimes[i], d))
            {
                a.push_back(d);
            }
        }
        Rng rng(seed, 0xC0);
        for (const ObservationSession& s : obs.sessions())
        {
            if (sessionsUsed.count(s.id) == 0)
            {
                continue;
            }
            for (double t = s.start + h->windowS; t < s.end - h->windowS; t += h->windowS * 0.5)
            {
                bool clear = true;
                for (size_t i = 0; i < eventTimes.size(); ++i)
                {
                    if (eventSessions[i] == s.id && std::fabs(eventTimes[i] - t) < h->windowS * 1.5)
                    {
                        clear = false;
                        break;
                    }
                }
                double d = 0.0;
                if (clear && rng.chance(0.7f) && deltaAt(s.id, t, d))
                {
                    b.push_back(d);
                }
            }
        }
        h->result = mannWhitney(a, b, seed);
        h->methodFr += fmt("Analyse « avant/après » : variation de la mesure dans les %.0f s suivant chaque événement (n = %.0f) comparée à des fenêtres témoins "
                           "sans événement (n = %.0f) ; test de Mann–Whitney par permutation.",
                           h->windowS, static_cast<double>(a.size()), static_cast<double>(b.size()));
        if (a.size() > 1)
        {
            h->warningsFr.push_back("Les événements détectés acoustiquement dépendent de la portée des micros : des appels lointains non détectés peuvent diluer l'effet.");
        }
    }
    else
    {
        std::vector<double> x;
        std::vector<double> y;
        for (const Tagged& t : samples)
        {
            x.push_back(measureOf(t.s, h->response, detections, t.session));
            y.push_back(measureOf(t.s, h->response2, detections, t.session));
        }
        h->result = spearman(x, y, seed);
        h->result.nA = static_cast<int>(x.size());
        h->methodFr += "Corrélation de rang de Spearman entre « " + std::string(measureLabelFr(h->response)) + " » et « " + measureLabelFr(h->response2) +
                       " », p par permutation, IC 95 % par bootstrap.";
        a = x;
    }

    // Status.
    const TestResult& r = h->result;
    const int nMin = h->kind == HypothesisKind::Correlation ? r.n : std::min(r.nA, r.nB);
    double signedEffect = r.effectSize;
    if (h->kind == HypothesisKind::Association)
    {
        signedEffect = std::log(std::max(1e-9, r.effectSize));
    }
    const int observedSign = signedEffect > 0.0 ? 1 : (signedEffect < 0.0 ? -1 : 0);
    if (nMin < 8)
    {
        h->status = HypothesisStatus::Open;
        h->warningsFr.push_back("Taille d'échantillon insuffisante (moins de 8 observations dans au moins une catégorie).");
    }
    else if (r.pValue < 0.05 && (!directional || observedSign == expectedSign))
    {
        h->status = HypothesisStatus::Supported;
    }
    else if (r.pValue < 0.05 && directional && observedSign != expectedSign)
    {
        h->status = HypothesisStatus::NotSupported;
        h->warningsFr.push_back("Effet significatif mais dans le sens opposé à la prédiction.");
    }
    else if (r.pValue > 0.2 && nMin >= 20)
    {
        h->status = HypothesisStatus::NotSupported;
    }
    else
    {
        h->status = HypothesisStatus::Inconclusive;
    }
    if (h->status == HypothesisStatus::Supported)
    {
        h->evidence = (r.pValue < 0.01 && nMin >= 30 && h->warningsFr.empty()) ? Confidence::StronglySupported : Confidence::Inferred;
        if (expeditionId != 0 && std::find(h->expeditionsSupporting.begin(), h->expeditionsSupporting.end(), expeditionId) == h->expeditionsSupporting.end())
        {
            h->expeditionsSupporting.push_back(expeditionId);
        }
    }
    else if (h->status == HypothesisStatus::Open)
    {
        h->evidence = Confidence::Unknown;
    }
    else
    {
        h->evidence = Confidence::Speculative;
    }
    std::string interp;
    switch (h->status)
    {
    case HypothesisStatus::Open: interp = "Les données sont encore trop peu nombreuses pour évaluer la prédiction. Il faut retourner sur le terrain."; break;
    case HypothesisStatus::Supported:
        interp = fmt("Dans les conditions observées, les données vont dans le sens de la prédiction (p = %.3f ; effet = %.2f, IC 95 %% [%.2f ; %.2f]). ", r.pValue,
                     r.effectSize, r.ciLow, r.ciHigh);
        interp += "Ce résultat décrit une régularité ; il ne démontre pas le mécanisme. D'autres explications restent possibles.";
        break;
    case HypothesisStatus::NotSupported:
        interp = fmt("Les données ne soutiennent pas la prédiction (p = %.3f ; effet = %.2f, IC 95 %% [%.2f ; %.2f]). ", r.pValue, r.effectSize, r.ciLow, r.ciHigh);
        interp += "Un effet faible ne peut pas être exclu ; une hypothèse alternative peut être formulée.";
        break;
    case HypothesisStatus::Inconclusive:
        interp = fmt("Résultat non concluant (p = %.3f ; effet = %.2f). Davantage de données, ou un protocole plus contrôlé, sont nécessaires.", r.pValue, r.effectSize);
        break;
    }
    if (h->expeditionsSupporting.size() >= 2)
    {
        interp += " Le résultat a été répliqué lors de plusieurs expéditions indépendantes.";
    }
    h->interpretationFr = interp;
}

std::string HypothesisSystem::describe(const Hypothesis& h, const EvidenceDatabase* db)
{
    std::string species = "toutes espèces";
    if (db && h.speciesIndex >= 0)
    {
        species = db->speciesAt(h.speciesIndex).scientificName;
    }
    std::string s = "[" + species + "] ";
    if (!h.statementFr.empty())
    {
        s += h.statementFr;
    }
    else
    {
        s += measureLabelFr(h.response);
    }
    s += " — ";
    s += hypothesisStatusLabelFr(h.status);
    return s;
}
} // namespace noctis
