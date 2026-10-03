#include "Noctis/Research/Expedition.h"

namespace noctis
{
const char* dataTargetLabelFr(DataTargetKind k)
{
    switch (k)
    {
    case DataTargetKind::Recordings: return "enregistrements sonores";
    case DataTargetKind::AcousticDetections: return "vocalisations détectées";
    case DataTargetKind::Photos: return "photographies";
    case DataTargetKind::IdentifiedIndividuals: return "individus identifiés";
    case DataTargetKind::SessionHours: return "heures d'observation";
    case DataTargetKind::Trackways: return "pistes mesurées";
    case DataTargetKind::Specimens: return "spécimens collectés";
    case DataTargetKind::Observations: return "observations";
    }
    return "?";
}

const char* expeditionStatusLabelFr(ExpeditionStatus s)
{
    switch (s)
    {
    case ExpeditionStatus::Planned: return "Planifiée";
    case ExpeditionStatus::Active: return "En cours";
    case ExpeditionStatus::Completed: return "Terminée";
    case ExpeditionStatus::Aborted: return "Interrompue";
    }
    return "?";
}

u32 ExpeditionSystem::plan(Expedition e, const EquipmentCatalog& catalog, float vehiclePayloadKg)
{
    e.id = nextId_++;
    e.number = static_cast<int>(e.id);
    e.status = ExpeditionStatus::Planned;
    e.loadoutSummary = summarizeLoadout(e.loadout, catalog, vehiclePayloadKg);
    expeditions_.push_back(e);
    return e.id;
}

bool ExpeditionSystem::start(u32 id, double now, const std::string& timeText)
{
    if (active())
    {
        return false;
    }
    Expedition* e = find(id);
    if (!e || e->status != ExpeditionStatus::Planned)
    {
        return false;
    }
    e->status = ExpeditionStatus::Active;
    e->start = now;
    log(id, now, timeText, "Début de l'expédition. Question : " + e->questionFr, true);
    for (const std::string& w : e->loadoutSummary.warningsFr)
    {
        log(id, now, timeText, "Avertissement matériel : " + w, true);
    }
    return true;
}

void ExpeditionSystem::end(u32 id, double now, const std::string& timeText, bool aborted)
{
    Expedition* e = find(id);
    if (!e || e->status != ExpeditionStatus::Active)
    {
        return;
    }
    e->status = aborted ? ExpeditionStatus::Aborted : ExpeditionStatus::Completed;
    e->end = now;
    log(id, now, timeText, aborted ? "Expédition interrompue." : "Fin de l'expédition.", true);
}

void ExpeditionSystem::log(u32 id, double now, const std::string& timeText, const std::string& text, bool automatic)
{
    if (Expedition* e = find(id))
    {
        e->log.push_back({now, timeText, text, automatic});
    }
}

Expedition* ExpeditionSystem::active()
{
    for (Expedition& e : expeditions_)
    {
        if (e.status == ExpeditionStatus::Active)
        {
            return &e;
        }
    }
    return nullptr;
}

const Expedition* ExpeditionSystem::active() const
{
    for (const Expedition& e : expeditions_)
    {
        if (e.status == ExpeditionStatus::Active)
        {
            return &e;
        }
    }
    return nullptr;
}

Expedition* ExpeditionSystem::find(u32 id)
{
    for (Expedition& e : expeditions_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

const Expedition* ExpeditionSystem::find(u32 id) const
{
    for (const Expedition& e : expeditions_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}
} // namespace noctis
