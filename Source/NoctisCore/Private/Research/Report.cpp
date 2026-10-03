// Scientific report generation from the data the player actually collected.
#include "Noctis/Research/ResearchSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace noctis
{
namespace reportimpl
{
std::string f(const char* fmtStr, double a, double b = 0.0, double c = 0.0, double d = 0.0)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), fmtStr, a, b, c, d);
    return buf;
}

std::string cite(const EvidenceDatabase& db, const std::vector<std::string>& ids, std::set<std::string>& used)
{
    std::string out;
    for (const std::string& id : ids)
    {
        const SourceRef* s = db.findSource(id);
        if (!s)
        {
            continue;
        }
        used.insert(id);
        if (!out.empty())
        {
            out += " ; ";
        }
        out += s->shortCitation();
    }
    return out.empty() ? out : " (" + out + ")";
}
} // namespace reportimpl

ScientificReport ResearchSystem::generateReport(u32 expeditionId, const WorldContext& ctx) const
{
    using reportimpl::f;
    ScientificReport rep;
    rep.expeditionId = expeditionId;
    rep.generatedAt = ctx.now;
    const Expedition* exp = expeditions_.find(expeditionId);
    if (!exp || !db_)
    {
        rep.markdown = "Expédition introuvable.";
        return rep;
    }
    const EvidenceDatabase& db = *db_;
    std::set<std::string> cited;
    std::string md;
    JsonValue js = JsonValue::makeObject();
    rep.titleFr = "Rapport scientifique — Expédition n°" + std::to_string(exp->number) + (exp->titleFr.empty() ? std::string() : " : " + exp->titleFr);
    md += "# " + rep.titleFr + "\n\n";
    md += "- **Environnement** : " + (env_ ? env_->nameFr + " (reconstruction " + env_->reconstructionVersion + ")" : std::string("?")) + "\n";
    md += "- **Profil de reconstruction** : " + exp->profileId + " v" + exp->profileVersion + "\n";
    md += "- **Zone** : " + (exp->zoneNameFr.empty() ? std::string("vallée entière") : exp->zoneNameFr) + "\n";
    md += f("- **Durée effective** : %.1f jours simulés\n", ((exp->end > 0.0 ? exp->end : ctx.now) - exp->start) / 86400.0);
    md += "- **Statut** : " + std::string(expeditionStatusLabelFr(exp->status)) + "\n\n";
    md += "> Les données de ce rapport proviennent d'un écosystème **simulé**, reconstruit à partir de la littérature scientifique. "
          "Les résultats décrivent le comportement du monde simulé sous le profil de reconstruction indiqué ; ils ne constituent pas "
          "des observations du Crétacé. Les éléments de la reconstruction qui sont spéculatifs sont listés dans la section Limites.\n\n";
    js.set("title", rep.titleFr);
    js.set("environment", env_ ? env_->id : "");
    js.set("profile", exp->profileId + "@" + exp->profileVersion);

    // 1. Question
    md += "## 1. Question\n\n" + (exp->questionFr.empty() ? std::string("Non précisée.") : exp->questionFr) + "\n\n";
    js.set("question", exp->questionFr);

    // 2. Context
    md += "## 2. Contexte\n\n";
    if (env_)
    {
        for (const char* topic : {"age", "geology", "paleolatitude", "climate", "landscape", "flora"})
        {
            const Claim* c = env_->claim(topic);
            if (c && !c->summary.empty())
            {
                md += "- **" + std::string(topic) + "** [" + confidenceLabelFr(c->confidence) + "] : " + c->summary + reportimpl::cite(db, c->sources, cited) + "\n";
            }
        }
    }
    md += "\n";

    // 3. Methods
    md += "## 3. Méthodologie\n\n";
    md += f("- Matériel embarqué : %.0f kg ; stockage %.0f Go ; %.0f micro(s) ; ", exp->loadoutSummary.massKg, exp->loadoutSummary.storageGb,
            exp->loadoutSummary.microphones);
    md += f("%.0f caméra(s) ; %.0f drone(s) ; %.0f piège(s) photographique(s).\n", exp->loadoutSummary.cameras, exp->loadoutSummary.drones,
            exp->loadoutSummary.cameraTraps);
    double sessionHours = 0.0;
    int sessionSamples = 0;
    std::map<ObservationMethod, int> methods;
    for (const u32 sid : exp->sessions)
    {
        for (const ObservationSession& s : observations_.sessions())
        {
            if (s.id == sid)
            {
                sessionHours += (s.end - s.start) / 3600.0;
                sessionSamples += static_cast<int>(s.samples.size());
            }
        }
    }
    for (const u32 oid : exp->observations)
    {
        if (const ObservationRecord* r = observations_.find(oid))
        {
            ++methods[r->method];
        }
    }
    md += f("- Effort : %.1f h de sessions d'observation (%.0f échantillons, au moins 30 s d'écart pour les tests) ; %.0f enregistrements ; ", sessionHours,
            sessionSamples, static_cast<double>(exp->recordings.size()));
    md += f("%.1f min de vol de drone ; %.1f %% de la vallée explorée.\n", exp->droneFlightMin, map_.exploredFraction() * 100.0);
    md += "- Observations par méthode :";
    for (const auto& m : methods)
    {
        md += " " + std::string(observationMethodLabelFr(m.first)) + " (" + std::to_string(m.second) + ")";
    }
    md += "\n- Acoustique : spectrogrammes FFT 2048 points (Hann, recouvrement 75 %), détection à +8 dB au-dessus du bruit médian, f0 par produit "
          "spectral harmonique ; regroupement des appels par k-moyennes ; localisation par différences de temps d'arrivée (GCC-PHAT).\n";
    md += "- Identification individuelle : appariement de caractères observables (cicatrices, motifs, boiterie, taille) ; "
          "des erreurs d'identification sont possibles.\n\n";

    // 4. Observations
    md += "## 4. Observations\n\n| Espèce | Observations | Individus catalogués | Taille de groupe max. | Comportements observés |\n|---|---|---|---|---|\n";
    std::map<i16, int> obsBySpecies;
    std::map<i16, int> maxGroup;
    std::map<i16, std::map<ObservedAction, int>> actions;
    for (const u32 oid : exp->observations)
    {
        const ObservationRecord* r = observations_.find(oid);
        if (!r)
        {
            continue;
        }
        std::set<i16> seen;
        for (const ObservedSubject& s : r->subjects)
        {
            if (s.speciesIndex < 0)
            {
                continue;
            }
            seen.insert(s.speciesIndex);
            ++actions[s.speciesIndex][s.action];
        }
        for (const i16 sp : seen)
        {
            ++obsBySpecies[sp];
        }
        for (const GroupObservation& g : r->groups)
        {
            maxGroup[g.speciesIndex] = std::max(maxGroup[g.speciesIndex], g.countEstimate);
        }
    }
    JsonValue jsObs = JsonValue::makeArray();
    for (const auto& o : obsBySpecies)
    {
        std::string ids;
        int catalogued = 0;
        for (const u32 cid : exp->catalogIds)
        {
            const CatalogEntry* e = observations_.catalog().find(cid);
            if (e && e->speciesIndex == o.first)
            {
                ++catalogued;
                if (catalogued <= 6)
                {
                    ids += (ids.empty() ? "" : ", ") + e->code + " « " + e->nickname + " »";
                }
            }
        }
        std::vector<std::pair<int, ObservedAction>> acts;
        for (const auto& a : actions[o.first])
        {
            acts.push_back({a.second, a.first});
        }
        std::sort(acts.rbegin(), acts.rend());
        std::string actText;
        for (size_t i = 0; i < acts.size() && i < 4; ++i)
        {
            actText += (i ? ", " : "") + std::string(observedActionLabelFr(acts[i].second)) + " (" + std::to_string(acts[i].first) + ")";
        }
        md += "| *" + db.speciesAt(o.first).scientificName + "* | " + std::to_string(o.second) + " | " + std::to_string(catalogued) +
              (ids.empty() ? "" : " : " + ids) + " | " + std::to_string(maxGroup[o.first]) + " | " + actText + " |\n";
        JsonValue row = JsonValue::makeObject();
        row.set("species", db.speciesAt(o.first).id);
        row.set("observations", o.second);
        row.set("catalogued", catalogued);
        row.set("max_group", maxGroup[o.first]);
        jsObs.push(row);
    }
    js.set("observations", jsObs);
    md += "\nÉvénements notables (journal) :\n\n";
    int logged = 0;
    for (const LogEntry& l : exp->log)
    {
        if (logged++ > 40)
        {
            md += "- …\n";
            break;
        }
        md += "- " + l.timeText + " — " + l.textFr + "\n";
    }
    md += "\n";

    // 5. Acoustic data
    md += "## 5. Données acoustiques\n\n";
    std::map<int, std::vector<const AcousticDetection*>> byCluster;
    int nDet = 0;
    for (const AcousticDetection& d : detections_)
    {
        if (std::find(exp->recordings.begin(), exp->recordings.end(), d.recordingId) == exp->recordings.end())
        {
            continue;
        }
        ++nDet;
        byCluster[d.cluster].push_back(&d);
    }
    md += f("%.0f événements sonores détectés dans %.0f enregistrements.\n\n", nDet, static_cast<double>(exp->recordings.size()));
    if (nDet > 0)
    {
        md += "| Type (regroupement) | n | f0 médiane (Hz) | durée médiane (s) | niveau médian (dB SPL au micro) |\n|---|---|---|---|---|\n";
        for (const auto& c : byCluster)
        {
            std::vector<double> f0;
            std::vector<double> dur;
            std::vector<double> lvl;
            for (const AcousticDetection* d : c.second)
            {
                f0.push_back(d->f0Hz);
                dur.push_back(d->durationS);
                lvl.push_back(d->levelDb);
            }
            md += (c.first < 0 ? std::string("non regroupé") : "Type " + std::string(1, static_cast<char>('A' + c.first))) + " | " +
                  std::to_string(c.second.size()) + f(" | %.0f | %.2f | %.0f |\n", median(f0), median(dur), median(lvl));
        }
        md += "\nAucune signification n'est attribuée automatiquement à ces types : leur fonction éventuelle relève des hypothèses ci-dessous.\n\n";
    }

    // 6. Traces and remains
    md += "## 6. Pistes, restes et spécimens\n\n";
    for (const u32 tid : exp->trackways)
    {
        if (tid == 0 || tid > trackways_.size())
        {
            continue;
        }
        const TrackwayAnalysis& t = trackways_[tid - 1];
        if (!t.valid)
        {
            continue;
        }
        md += "- Piste n°" + std::to_string(tid) + " : " + t.makerGuessFr +
              f(", foulée %.2f m, hauteur de hanche estimée %.2f m, vitesse %.1f m/s (intervalle %.1f–", t.strideM, t.hipHeightEstM, t.speedEstMs, t.speedLowMs) +
              f("%.1f m/s), allure : ", t.speedHighMs) + t.gaitFr + ".\n";
        for (const std::string& c : t.caveatsFr)
        {
            md += "  - " + c + "\n";
        }
    }
    for (const CarcassExamination& e : examinations_)
    {
        md += "- Carcasse : " + e.stageFr + f(", délai depuis la mort estimé %.0f j (%.0f–%.0f j)", e.timeSinceDeathDays, e.timeSinceDeathLowDays,
                                              e.timeSinceDeathHighDays) +
              f(", %.0f trace(s) de morsure, %.0f dent(s) isolée(s).", static_cast<double>(e.bites.marks), static_cast<double>(e.shedTeeth)) + "\n";
        if (!e.bites.interpretationFr.empty())
        {
            md += "  - " + e.bites.interpretationFr + "\n";
        }
    }
    for (const u32 sid : exp->specimens)
    {
        for (const FossilSpecimen& s : paleo_.specimens())
        {
            if (s.id == sid)
            {
                md += "- Spécimen n°" + std::to_string(s.id) + " (" + specimenElementLabelFr(s.element) + ") : " +
                      (s.identified ? s.identificationFr + " [" + confidenceLabelFr(s.identificationConfidence) + "]" : std::string("non identifié")) + "\n";
            }
        }
    }
    md += "\n";

    // 7. Hypotheses
    md += "## 7. Hypothèses et résultats\n\n";
    Confidence overall = Confidence::Unknown;
    bool anyTested = false;
    JsonValue jsHyp = JsonValue::makeArray();
    std::vector<std::string> openQuestions;
    for (const u32 hid : exp->hypotheses)
    {
        const Hypothesis* h = hypotheses_.find(hid);
        if (!h)
        {
            continue;
        }
        md += "### H" + std::to_string(h->id) + " — " + h->statementFr + "\n\n";
        md += "- Statut : **" + std::string(hypothesisStatusLabelFr(h->status)) + "** — niveau de preuve : " + confidenceLabelFr(h->evidence) + "\n";
        md += "- Méthode : " + h->methodFr + "\n";
        md += f("- Effectifs : n = %.0f (A = %.0f, B = %.0f) ; ", h->result.n, h->result.nA, h->result.nB) + h->result.method +
              f(" : p = %.4f ; effet = %.2f [%.2f ; %.2f]\n", h->result.pValue, h->result.effectSize, h->result.ciLow, h->result.ciHigh);
        md += "- Interprétation : " + h->interpretationFr + "\n";
        for (const std::string& w : h->warningsFr)
        {
            md += "- ⚠ " + w + "\n";
        }
        md += "\n";
        if (h->tests > 0)
        {
            anyTested = true;
            overall = h->status == HypothesisStatus::Supported ? (overall == Confidence::Unknown ? h->evidence : weakest(overall, h->evidence)) : overall;
        }
        if (h->status != HypothesisStatus::Supported)
        {
            openQuestions.push_back(h->statementFr);
        }
        JsonValue jh = JsonValue::makeObject();
        jh.set("id", h->id);
        jh.set("statement", h->statementFr);
        jh.set("status", hypothesisStatusLabelFr(h->status));
        jh.set("evidence", confidenceKey(h->evidence));
        jh.set("p", h->result.pValue);
        jh.set("effect", h->result.effectSize);
        jh.set("n", h->result.n);
        jsHyp.push(jh);
    }
    if (exp->hypotheses.empty())
    {
        md += "Aucune hypothèse formulée pendant cette expédition.\n\n";
    }
    js.set("hypotheses", jsHyp);

    // 8. Limitations
    md += "## 8. Limites\n\n";
    if (sessionSamples < 30)
    {
        md += "- Taille d'échantillon réduite : les conclusions restent fragiles.\n";
    }
    if (exp->droneFlightMin > 0.0)
    {
        md += f("- Le drone a volé %.0f min : il est audible et visible par les animaux ; son effet sur les comportements doit être contrôlé.\n", exp->droneFlightMin);
    }
    int unidentified = 0;
    int subjects = 0;
    for (const u32 oid : exp->observations)
    {
        if (const ObservationRecord* r = observations_.find(oid))
        {
            for (const ObservedSubject& s : r->subjects)
            {
                ++subjects;
                unidentified += s.catalogId == 0 ? 1 : 0;
            }
        }
    }
    if (subjects > 0)
    {
        md += f("- %.0f %% des animaux observés n'ont pas pu être identifiés individuellement ; pseudo-réplication possible.\n",
                100.0 * unidentified / subjects);
    }
    if (ctx.profile)
    {
        md += "- Éléments spéculatifs ou inférés actifs dans la reconstruction :\n";
        for (const ProfileToggle& t : ctx.profile->toggles)
        {
            if (t.enabled)
            {
                md += "  - " + t.descriptionFr + " [" + confidenceLabelFr(t.confidence) + "]" + reportimpl::cite(db, t.sources, cited) + "\n";
            }
        }
    }
    md += "- Les vocalisations du monde simulé sont des hypothèses de reconstruction (aucune vocalisation de dinosaure non aviaire n'est connue directement).\n\n";

    // 9-11.
    rep.overallConfidence = anyTested ? overall : Confidence::Unknown;
    md += "## 9. Niveau de confiance global\n\n" + std::string(confidenceLabelFr(rep.overallConfidence)) +
          (anyTested ? " — déterminé par l'hypothèse soutenue la moins solide." : " — aucune hypothèse testée.") + "\n\n";
    md += "## 10. Conclusion\n\n";
    int supported = 0;
    for (const u32 hid : exp->hypotheses)
    {
        const Hypothesis* h = hypotheses_.find(hid);
        supported += (h && h->status == HypothesisStatus::Supported) ? 1 : 0;
    }
    if (supported > 0)
    {
        md += f("%.0f hypothèse(s) sont soutenues par les données de cette expédition, dans les conditions observées. Elles décrivent des régularités "
                "et appellent une réplication indépendante avant toute généralisation.\n\n",
                supported);
    }
    else
    {
        md += "Les données collectées ne permettent pas encore de conclure. Elles constituent une base pour une nouvelle campagne de terrain.\n\n";
    }
    md += "## 11. Questions ouvertes\n\n";
    for (const std::string& q : openQuestions)
    {
        md += "- " + q + "\n";
    }
    md += "- Les effets observés persistent-ils sans drone ni véhicule à proximité ?\n";
    md += "- Les individus catalogués occupent-ils les mêmes zones lors d'une autre saison ?\n\n";
    md += "## Références\n\n";
    for (const std::string& id : cited)
    {
        if (const SourceRef* s = db.findSource(id))
        {
            md += "- " + s->fullCitation() + "\n";
        }
    }
    rep.markdown = md;
    js.set("overall_confidence", confidenceKey(rep.overallConfidence));
    rep.json = js;
    return rep;
}
} // namespace noctis
