#include "Noctis/Science/EvidenceDatabase.h"

#include "Noctis/Core/Log.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace noctis
{
namespace evidenceimpl
{
std::vector<std::string> readStringList(const JsonValue& v)
{
    std::vector<std::string> out;
    if (v.isArray())
    {
        for (const JsonValue& item : v.items())
        {
            if (item.isString())
            {
                out.push_back(item.asString());
            }
        }
    }
    else if (v.isString() && !v.asString().empty())
    {
        out.push_back(v.asString());
    }
    return out;
}

void readFloat(const JsonValue& obj, const char* key, float& target)
{
    const JsonValue& v = obj.get(key);
    if (v.isNumber())
    {
        target = v.asFloat(target);
    }
}

void readBool(const JsonValue& obj, const char* key, bool& target)
{
    const JsonValue& v = obj.get(key);
    if (v.isBool())
    {
        target = v.asBool(target);
    }
}

TraitDistribution readTrait(const JsonValue& v, TraitDistribution fallback)
{
    if (v.isArray() && v.size() >= 2)
    {
        return {v.at(0).asFloat(fallback.mean), v.at(1).asFloat(fallback.sd)};
    }
    if (v.isNumber())
    {
        return {v.asFloat(fallback.mean), fallback.sd};
    }
    return fallback;
}

std::string derivePrefix(const std::string& scientificName)
{
    std::string prefix;
    for (const char c : scientificName)
    {
        if (c == ' ')
        {
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(c)))
        {
            prefix.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        if (prefix.size() == 3)
        {
            break;
        }
    }
    return prefix.empty() ? std::string("UNK") : prefix;
}

void collectBasisKeys(const JsonValue& v, const std::string& prefix, std::vector<std::string>& out)
{
    if (!v.isObject())
    {
        return;
    }
    for (const auto& m : v.members())
    {
        const std::string path = prefix.empty() ? m.first : prefix + "." + m.first;
        if (m.second.isObject() && m.first != "plant_preferences" && m.first != "habitat" && m.first != "personality")
        {
            collectBasisKeys(m.second, path, out);
        }
        else
        {
            out.push_back(path);
        }
    }
}
} // namespace evidenceimpl

const std::vector<std::string>& requiredSpeciesTopics()
{
    static const std::vector<std::string> topics = {
        "taxonomy", "period", "formation", "location", "size", "mass", "morphology", "skeleton", "musculature",
        "soft_tissue", "integument", "locomotion", "speed", "acceleration", "diet", "feeding_strategy", "predation",
        "defense", "social_behavior", "reproduction", "development", "growth", "senses", "communication",
        "activity_pattern", "habitat", "climate", "interactions", "fossil_record", "documented_behavior",
        "inferred_behavior"};
    return topics;
}

std::string SourceRef::shortCitation() const
{
    std::string first = authors;
    const size_t sep = first.find_first_of(",;");
    if (sep != std::string::npos)
    {
        first = first.substr(0, sep);
    }
    const bool many = authors.find(';') != std::string::npos;
    const size_t semicolons = static_cast<size_t>(std::count(authors.begin(), authors.end(), ';'));
    std::string s = first;
    if (many && semicolons >= 2)
    {
        s += " et al.";
    }
    else if (many)
    {
        std::string second = authors.substr(authors.find(';') + 1);
        while (!second.empty() && second.front() == ' ')
        {
            second.erase(second.begin());
        }
        const size_t comma = second.find(',');
        s += " & " + (comma == std::string::npos ? second : second.substr(0, comma));
    }
    return s + " " + std::to_string(year);
}

std::string SourceRef::fullCitation() const
{
    std::string s = authors + " (" + std::to_string(year) + "). " + title + ". " + container;
    if (!volume.empty())
    {
        s += " " + volume;
    }
    if (!pages.empty())
    {
        s += ": " + pages;
    }
    s += ".";
    if (!doi.empty())
    {
        s += " doi:" + doi;
    }
    if (!verified)
    {
        s += " [référence non vérifiée]";
    }
    return s;
}

bool ReconstructionProfile::enabled(const std::string& toggleId) const
{
    const ProfileToggle* t = find(toggleId);
    return t != nullptr && t->enabled;
}

const ProfileToggle* ReconstructionProfile::find(const std::string& toggleId) const
{
    for (const ProfileToggle& t : toggles)
    {
        if (t.id == toggleId)
        {
            return &t;
        }
    }
    return nullptr;
}

Claim parseClaim(const JsonValue& v)
{
    Claim c;
    if (v.isString())
    {
        c.summary = v.asString();
        return c;
    }
    c.summary = v.getString("summary");
    c.confidence = parseConfidence(v.getString("confidence", "unknown"));
    c.sources = evidenceimpl::readStringList(v.get("sources"));
    c.unit = v.getString("unit");
    c.notes = v.getString("notes");
    if (v.get("value").isNumber())
    {
        c.hasValue = true;
        c.value = v.get("value").asDouble();
    }
    if (v.get("min").isNumber())
    {
        c.hasMin = true;
        c.min = v.get("min").asDouble();
    }
    if (v.get("max").isNumber())
    {
        c.hasMax = true;
        c.max = v.get("max").asDouble();
    }
    return c;
}

JsonValue claimToJson(const Claim& c)
{
    JsonValue o = JsonValue::makeObject();
    o.set("summary", c.summary);
    o.set("confidence", confidenceKey(c.confidence));
    JsonValue src = JsonValue::makeArray();
    for (const std::string& s : c.sources)
    {
        src.push(s);
    }
    o.set("sources", src);
    if (c.hasValue)
    {
        o.set("value", c.value);
    }
    if (c.hasMin)
    {
        o.set("min", c.min);
    }
    if (c.hasMax)
    {
        o.set("max", c.max);
    }
    if (!c.unit.empty())
    {
        o.set("unit", c.unit);
    }
    if (!c.notes.empty())
    {
        o.set("notes", c.notes);
    }
    return o;
}

bool parseSpeciesJson(const JsonValue& root, SpeciesDefinition& out, std::vector<std::string>& errors)
{
    using evidenceimpl::readBool;
    using evidenceimpl::readFloat;
    if (!root.isObject())
    {
        errors.push_back("root is not an object");
        return false;
    }
    if (root.getString("schema") != "noctis.species/1")
    {
        errors.push_back("unexpected schema '" + root.getString("schema") + "'");
    }
    out.id = root.getString("id");
    if (out.id.empty())
    {
        errors.push_back("missing id");
        return false;
    }
    out.reconstructionVersion = root.getString("reconstruction_version", "unversioned");
    out.lastReviewed = root.getString("last_reviewed");
    out.scientificName = root.getString("scientific_name", out.id);
    out.authority = root.getString("authority");
    out.commonNameFr = root.get("common_names").getString("fr", out.scientificName);
    out.commonNameEn = root.get("common_names").getString("en", out.scientificName);
    out.catalogPrefix = root.getString("catalog_prefix", evidenceimpl::derivePrefix(out.scientificName));
    out.regions = evidenceimpl::readStringList(root.get("regions"));

    for (const auto& m : root.get("science").members())
    {
        out.science.emplace_back(m.first, parseClaim(m.second));
    }
    for (const JsonValue& h : root.get("competing_hypotheses").items())
    {
        CompetingHypothesis ch;
        ch.topic = h.getString("topic");
        ch.summary = h.getString("summary");
        for (const JsonValue& p : h.get("positions").items())
        {
            ch.positions.push_back({p.getString("summary"), evidenceimpl::readStringList(p.get("sources"))});
        }
        out.competing.push_back(std::move(ch));
    }

    const JsonValue& s = root.get("sim");
    SpeciesSim& sim = out.sim;
    sim.role = s.getString("role", "agent") == "population" ? SimRole::Population : SimRole::Agent;
    bool bodyOk = true;
    sim.bodyPlan = parseBodyPlan(s.getString("body_plan"), &bodyOk);
    if (!bodyOk)
    {
        errors.push_back("unknown body_plan '" + s.getString("body_plan") + "'");
    }
    readFloat(s, "adult_mass_kg", sim.adultMassKg);
    readFloat(s, "adult_length_m", sim.adultLengthM);
    readFloat(s, "hip_height_m", sim.hipHeightM);
    readFloat(s, "foot_length_m", sim.footLengthM);
    readFloat(s, "foot_width_m", sim.footWidthM);
    readFloat(s, "hatchling_mass_kg", sim.hatchlingMassKg);
    readFloat(s, "sexual_dimorphism_mass", sim.sexualDimorphismMass);

    const JsonValue& g = s.get("growth");
    sim.growth.asymptoticMassKg = sim.adultMassKg;
    readFloat(g, "asymptotic_mass_kg", sim.growth.asymptoticMassKg);
    readFloat(g, "max_growth_rate_kg_per_year", sim.growth.maxGrowthRateKgPerYear);
    readFloat(g, "inflection_age_years", sim.growth.inflectionAgeYears);
    readFloat(g, "maturity_age_years", sim.growth.maturityAgeYears);
    readFloat(g, "max_lifespan_years", sim.growth.maxLifespanYears);

    const JsonValue& l = s.get("locomotion");
    readFloat(l, "walk_speed_ms", sim.locomotion.walkSpeedMs);
    readFloat(l, "max_speed_ms", sim.locomotion.maxSpeedMs);
    readFloat(l, "max_accel_ms2", sim.locomotion.maxAccelMs2);
    readFloat(l, "max_decel_ms2", sim.locomotion.maxDecelMs2);
    readFloat(l, "max_yaw_rate_dps", sim.locomotion.maxYawRateDps);
    readFloat(l, "max_slope_deg", sim.locomotion.maxSlopeDeg);
    readBool(l, "swim", sim.locomotion.swim);
    readFloat(l, "swim_speed_ms", sim.locomotion.swimSpeedMs);
    readBool(l, "fly", sim.locomotion.fly);
    readFloat(l, "fly_speed_ms", sim.locomotion.flySpeedMs);
    readFloat(l, "duty_factor_walk", sim.locomotion.dutyFactorWalk);
    readFloat(l, "duty_factor_run", sim.locomotion.dutyFactorRun);

    const JsonValue& met = s.get("metabolism");
    sim.metabolism.thermo = parseThermoClass(met.getString("thermo_class", "endotherm"));
    readFloat(met, "max_fasting_days", sim.metabolism.maxFastingDays);
    readFloat(met, "water_l_per_day_per_100kg", sim.metabolism.waterLPerDayPer100kg);

    const JsonValue& d = s.get("diet");
    sim.diet.type = parseDietType(d.getString("type", "herbivore"));
    for (const auto& m : d.get("plant_preferences").members())
    {
        const int pt = parsePlantType(m.first);
        if (pt >= 0)
        {
            sim.diet.plantPreference[pt] = m.second.asFloat(0.0f);
        }
        else
        {
            errors.push_back("unknown plant type '" + m.first + "'");
        }
    }
    readFloat(d, "browse_height_min_m", sim.diet.browseMinM);
    readFloat(d, "browse_height_max_m", sim.diet.browseMaxM);
    readFloat(d, "prey_mass_min_kg", sim.diet.preyMassMinKg);
    readFloat(d, "prey_mass_max_kg", sim.diet.preyMassMaxKg);
    readBool(d, "scavenges", sim.diet.scavenges);
    readBool(d, "eats_fish", sim.diet.eatsFish);
    readBool(d, "eats_invertebrates", sim.diet.eatsInvertebrates);
    readBool(d, "eats_eggs", sim.diet.eatsEggs);

    const JsonValue& se = s.get("senses");
    readFloat(se, "vision_range_m", sim.senses.visionRangeM);
    readFloat(se, "fov_deg", sim.senses.fovDeg);
    readFloat(se, "binocular_deg", sim.senses.binocularDeg);
    readFloat(se, "low_light", sim.senses.lowLight);
    readFloat(se, "hearing_min_hz", sim.senses.hearingMinHz);
    readFloat(se, "hearing_max_hz", sim.senses.hearingMaxHz);
    readFloat(se, "hearing_best_hz", sim.senses.hearingBestHz);
    readFloat(se, "hearing_threshold_db", sim.senses.hearingThresholdDb);
    readFloat(se, "olfaction", sim.senses.olfaction);
    readFloat(se, "vibration", sim.senses.vibration);

    const JsonValue& so = s.get("social");
    readFloat(so, "group_size_mean", sim.social.groupSizeMean);
    readFloat(so, "group_size_max", sim.social.groupSizeMax);
    readFloat(so, "cohesion", sim.social.cohesion);
    readBool(so, "territorial", sim.social.territorial);
    readFloat(so, "territory_radius_m", sim.social.territoryRadiusM);
    readBool(so, "alarm_calls", sim.social.alarmCalls);

    const JsonValue& de = s.get("defense");
    sim.defense.weapons = 0;
    for (const std::string& w : evidenceimpl::readStringList(de.get("weapons")))
    {
        const u16 bit = parseWeapon(w);
        if (bit == 0)
        {
            errors.push_back("unknown weapon '" + w + "'");
        }
        sim.defense.weapons = static_cast<u16>(sim.defense.weapons | bit);
    }
    readFloat(de, "armor", sim.defense.armor);
    readFloat(de, "defense_strength", sim.defense.defenseStrength);
    readFloat(de, "flee_preference", sim.defense.fleePreference);

    const JsonValue& pr = s.get("predation");
    sim.predation.style = parseHuntStyle(pr.getString("hunt_style", "none"));
    readFloat(pr, "attack_strength", sim.predation.attackStrength);
    readFloat(pr, "bite_force_n", sim.predation.biteForceN);
    readFloat(pr, "max_chase_s", sim.predation.maxChaseS);

    const JsonValue& re = s.get("reproduction");
    readFloat(re, "clutch_size_mean", sim.reproduction.clutchSizeMean);
    readFloat(re, "clutch_size_max", sim.reproduction.clutchSizeMax);
    readFloat(re, "incubation_days", sim.reproduction.incubationDays);
    if (re.get("breeding_season_doy").isArray() && re.get("breeding_season_doy").size() >= 2)
    {
        sim.reproduction.breedingStartDoy = re.get("breeding_season_doy").at(0).asFloat(sim.reproduction.breedingStartDoy);
        sim.reproduction.breedingEndDoy = re.get("breeding_season_doy").at(1).asFloat(sim.reproduction.breedingEndDoy);
    }
    readFloat(re, "parental_care", sim.reproduction.parentalCare);
    sim.reproduction.nest = parseNestType(re.getString("nest_type", "open_scrape"));

    const JsonValue& vo = s.get("vocal");
    readBool(vo, "closed_mouth", sim.vocal.closedMouth);
    readFloat(vo, "f0_min_hz", sim.vocal.f0MinHz);
    readFloat(vo, "f0_max_hz", sim.vocal.f0MaxHz);
    for (const JsonValue& c : vo.get("call_types").items())
    {
        CallTypeSpec call;
        call.id = c.getString("id", "call");
        call.contextText = c.getString("context", call.id);
        call.context = parseCallContext(call.contextText);
        if (call.context == CallContext::Other)
        {
            call.context = parseCallContext(call.id);
        }
        call.durationS = c.getFloat("duration_s", call.durationS);
        call.f0Hz = c.getFloat("f0_hz", call.f0Hz);
        call.sourceLevelDb = c.getFloat("source_level_db", call.sourceLevelDb);
        call.confidence = parseConfidence(c.getString("confidence", "speculative"));
        sim.vocal.calls.push_back(call);
    }

    sim.activity = parseActivityPattern(s.get("activity").getString("pattern", "diurnal"));

    const JsonValue& pe = s.get("personality");
    for (int a = 0; a < kPersonalityAxisCount; ++a)
    {
        sim.personality[a] = evidenceimpl::readTrait(pe.get(personalityKey(static_cast<PersonalityAxis>(a))), sim.personality[a]);
    }

    for (const auto& m : s.get("habitat").members())
    {
        const int h = parseHabitat(m.first);
        if (h >= 0)
        {
            sim.habitat[h] = m.second.asFloat(0.0f);
        }
        else
        {
            errors.push_back("unknown habitat '" + m.first + "'");
        }
    }

    const JsonValue& po = s.get("population");
    readFloat(po, "initial_count", sim.population.initialCount);
    readFloat(po, "regional_density_per_km2", sim.population.regionalDensityPerKm2);

    for (const auto& m : root.get("sim_basis").members())
    {
        out.simBasis.emplace_back(m.first, m.second.isString() ? m.second.asString() : m.second.dump(false));
    }

    // Sanity: physical parameters must be positive.
    if (sim.adultMassKg <= 0.0f || sim.adultLengthM <= 0.0f)
    {
        errors.push_back("adult mass/length must be positive");
    }
    if (sim.hipHeightM <= 0.0f)
    {
        sim.hipHeightM = std::max(0.05f, 0.25f * sim.adultLengthM);
    }
    if (sim.footLengthM <= 0.0f)
    {
        sim.footLengthM = sim.hipHeightM * 0.25f;
    }
    if (sim.footWidthM <= 0.0f)
    {
        sim.footWidthM = sim.footLengthM * 0.8f;
    }
    if (sim.hatchlingMassKg <= 0.0f)
    {
        sim.hatchlingMassKg = std::max(0.001f, sim.adultMassKg * 0.001f);
    }
    if (sim.growth.asymptoticMassKg <= 0.0f)
    {
        sim.growth.asymptoticMassKg = sim.adultMassKg;
    }
    return true;
}

bool parseEnvironmentJson(const JsonValue& root, EnvironmentDefinition& out, std::vector<std::string>& errors)
{
    using evidenceimpl::readFloat;
    if (!root.isObject())
    {
        errors.push_back("root is not an object");
        return false;
    }
    out.id = root.getString("id");
    if (out.id.empty())
    {
        errors.push_back("missing id");
        return false;
    }
    out.reconstructionVersion = root.getString("reconstruction_version", "unversioned");
    out.nameFr = root.get("name").getString("fr", out.id);
    out.nameEn = root.get("name").getString("en", out.id);
    for (const auto& m : root.get("science").members())
    {
        out.science.emplace_back(m.first, parseClaim(m.second));
    }
    const JsonValue& s = root.get("sim");
    EnvironmentSim& e = out.sim;
    readFloat(s, "latitude_deg", e.latitudeDeg);
    readFloat(s, "axial_tilt_deg", e.axialTiltDeg);
    readFloat(s, "mean_annual_temp_c", e.meanAnnualTempC);
    readFloat(s, "annual_temp_range_c", e.annualTempRangeC);
    readFloat(s, "diurnal_temp_range_c", e.diurnalTempRangeC);
    readFloat(s, "annual_precip_mm", e.annualPrecipMm);
    if (s.get("wet_season_doy").isArray() && s.get("wet_season_doy").size() >= 2)
    {
        e.wetSeasonStartDoy = s.get("wet_season_doy").at(0).asFloat(e.wetSeasonStartDoy);
        e.wetSeasonEndDoy = s.get("wet_season_doy").at(1).asFloat(e.wetSeasonEndDoy);
    }
    readFloat(s, "dry_season_strength", e.dryStrength);
    readFloat(s, "relative_humidity_mean", e.humidityMean);
    readFloat(s, "fog_morning_probability", e.fogMorningProbability);
    readFloat(s, "storm_probability_per_day", e.stormProbabilityPerDay);
    readFloat(s, "mean_wind_ms", e.meanWindMs);
    readFloat(s, "prevailing_wind_deg", e.prevailingWindDeg);
    e.terrainGenerator = s.getString("terrain_generator", e.terrainGenerator);
    readFloat(s, "map_size_m", e.mapSizeM);
    readFloat(s, "relief_m", e.reliefM);
    for (const JsonValue& h : s.get("habitats").items())
    {
        const int hi = parseHabitat(h.asString());
        if (hi >= 0)
        {
            e.habitats.push_back(static_cast<Habitat>(hi));
        }
        else
        {
            errors.push_back("unknown habitat '" + h.asString() + "'");
        }
    }
    for (const auto& m : s.get("plant_functional_types").members())
    {
        const int pt = parsePlantType(m.first);
        if (pt < 0)
        {
            errors.push_back("unknown plant functional type '" + m.first + "'");
            continue;
        }
        PlantTypeSpec& p = e.plants[pt];
        p.present = m.second.getBool("present", false);
        p.maxBiomassKgM2 = m.second.getFloat("max_biomass_kg_m2", 0.0f);
        p.growthRatePerDay = m.second.getFloat("growth_rate_per_day", 0.0f);
        p.heightM = m.second.getFloat("height_m", 0.0f);
        for (const auto& hm : m.second.get("habitats").members())
        {
            const int hi = parseHabitat(hm.first);
            if (hi >= 0)
            {
                p.habitat[hi] = hm.second.asFloat(0.0f);
            }
        }
    }
    for (const JsonValue& sp : s.get("species").items())
    {
        if (sp.isString())
        {
            e.speciesIds.push_back(sp.asString());
        }
        else if (sp.isObject())
        {
            e.speciesIds.push_back(sp.getString("id"));
        }
    }
    const JsonValue& cal = s.get("calendar");
    e.calendar.dayLengthHours = cal.get("day_length_h").asDouble(e.calendar.dayLengthHours);
    e.calendar.daysPerYear = cal.get("days_per_year").asDouble(e.calendar.daysPerYear);
    e.calendar.synodicMonthDays = cal.get("synodic_month_days").asDouble(e.calendar.synodicMonthDays);
    for (const auto& m : root.get("sim_basis").members())
    {
        out.simBasis.emplace_back(m.first, m.second.isString() ? m.second.asString() : m.second.dump(false));
    }
    return true;
}

void EvidenceDatabase::issue(ValidationIssue::Severity s, const std::string& file, const std::string& msg)
{
    loadIssues_.push_back({s, file, msg});
}

bool EvidenceDatabase::loadBibliography(const FileSystem& fs, const std::string& dir)
{
    for (const std::string& path : fs.listFiles(dir, ".json", false))
    {
        std::string text;
        if (!fs.readText(path, text))
        {
            issue(ValidationIssue::Severity::Error, path, "cannot read file");
            continue;
        }
        JsonParseResult r = parseJson(text);
        if (!r.ok)
        {
            issue(ValidationIssue::Severity::Error, path, "JSON parse error " + r.error);
            continue;
        }
        for (const JsonValue& e : r.value.get("entries").items())
        {
            SourceRef s;
            s.id = e.getString("id");
            if (s.id.empty())
            {
                issue(ValidationIssue::Severity::Error, path, "bibliography entry without id");
                continue;
            }
            s.authors = e.getString("authors");
            s.year = e.getInt("year", 0);
            s.title = e.getString("title");
            s.container = e.getString("container");
            s.volume = e.get("volume").isNumber() ? std::to_string(e.get("volume").asInt()) : e.getString("volume");
            s.pages = e.getString("pages");
            s.doi = e.getString("doi");
            s.url = e.getString("url");
            s.type = e.getString("type", "article");
            s.verified = e.getBool("verified", false);
            s.verification = e.getString("verification");
            s.file = path;
            auto existing = sources_.find(s.id);
            if (existing != sources_.end())
            {
                // Same reference cited by several research passes: keep the verified/most complete one.
                if (!existing->second.verified && s.verified)
                {
                    existing->second = s;
                }
                continue;
            }
            sources_.emplace(s.id, s);
        }
    }
    return true;
}

bool EvidenceDatabase::loadSpecies(const FileSystem& fs, const std::string& dir)
{
    for (const std::string& path : fs.listFiles(dir, ".json", true))
    {
        std::string text;
        if (!fs.readText(path, text))
        {
            issue(ValidationIssue::Severity::Error, path, "cannot read file");
            continue;
        }
        JsonParseResult r = parseJson(text);
        if (!r.ok)
        {
            issue(ValidationIssue::Severity::Error, path, "JSON parse error " + r.error);
            continue;
        }
        SpeciesDefinition def;
        std::vector<std::string> errors;
        if (!parseSpeciesJson(r.value, def, errors))
        {
            for (const std::string& e : errors)
            {
                issue(ValidationIssue::Severity::Error, path, e);
            }
            continue;
        }
        for (const std::string& e : errors)
        {
            issue(ValidationIssue::Severity::Warning, path, e);
        }
        def.sourceFile = path;
        if (findSpecies(def.id) != nullptr)
        {
            issue(ValidationIssue::Severity::Error, path, "duplicate species id " + def.id);
            continue;
        }
        species_.push_back(std::move(def));
    }
    std::sort(species_.begin(), species_.end(), [](const SpeciesDefinition& a, const SpeciesDefinition& b) { return a.id < b.id; });
    return true;
}

bool EvidenceDatabase::loadEnvironments(const FileSystem& fs, const std::string& dir)
{
    for (const std::string& path : fs.listFiles(dir, ".json", false))
    {
        std::string text;
        if (!fs.readText(path, text))
        {
            continue;
        }
        JsonParseResult r = parseJson(text);
        if (!r.ok)
        {
            issue(ValidationIssue::Severity::Error, path, "JSON parse error " + r.error);
            continue;
        }
        EnvironmentDefinition env;
        std::vector<std::string> errors;
        if (!parseEnvironmentJson(r.value, env, errors))
        {
            for (const std::string& e : errors)
            {
                issue(ValidationIssue::Severity::Error, path, e);
            }
            continue;
        }
        for (const std::string& e : errors)
        {
            issue(ValidationIssue::Severity::Warning, path, e);
        }
        env.sourceFile = path;
        environments_.push_back(std::move(env));
    }
    return true;
}

bool EvidenceDatabase::loadProfiles(const FileSystem& fs, const std::string& dir)
{
    for (const std::string& path : fs.listFiles(dir, ".json", false))
    {
        std::string text;
        if (!fs.readText(path, text))
        {
            continue;
        }
        JsonParseResult r = parseJson(text);
        if (!r.ok)
        {
            issue(ValidationIssue::Severity::Error, path, "JSON parse error " + r.error);
            continue;
        }
        ReconstructionProfile p;
        p.id = r.value.getString("id");
        p.version = r.value.getString("version");
        p.nameFr = r.value.get("name").getString("fr", p.id);
        p.descriptionFr = r.value.getString("description_fr");
        for (const JsonValue& t : r.value.get("toggles").items())
        {
            ProfileToggle tog;
            tog.id = t.getString("id");
            tog.enabled = t.getBool("enabled", false);
            tog.confidence = parseConfidence(t.getString("confidence", "speculative"));
            tog.sources = evidenceimpl::readStringList(t.get("sources"));
            tog.descriptionFr = t.getString("description_fr");
            p.toggles.push_back(tog);
        }
        p.excludedSpecies = evidenceimpl::readStringList(r.value.get("excluded_species"));
        profiles_.push_back(std::move(p));
    }
    return true;
}

bool EvidenceDatabase::load(const FileSystem& fs, const std::string& dataRoot)
{
    species_.clear();
    environments_.clear();
    sources_.clear();
    profiles_.clear();
    loadIssues_.clear();
    loadBibliography(fs, joinPath(dataRoot, "Sources"));
    loadSpecies(fs, joinPath(dataRoot, "Species"));
    loadEnvironments(fs, joinPath(dataRoot, "Environments"));
    loadProfiles(fs, joinPath(dataRoot, "Profiles"));

    auto loadJsonFile = [&](const std::string& rel, JsonValue& target) {
        std::string text;
        const std::string path = joinPath(dataRoot, rel);
        if (!fs.readText(path, text))
        {
            issue(ValidationIssue::Severity::Info, path, "optional file not found");
            return;
        }
        JsonParseResult r = parseJson(text);
        if (!r.ok)
        {
            issue(ValidationIssue::Severity::Error, path, "JSON parse error " + r.error);
            return;
        }
        target = std::move(r.value);
    };
    loadJsonFile("Science/methods.json", methods_);
    loadJsonFile("Science/sim_constants.json", simConstants_);
    loadJsonFile("Equipment/equipment.json", equipment_);

    NOCTIS_LOG_INFO("EvidenceDatabase: %zu species, %zu environments, %zu sources, %zu profiles", species_.size(),
                    environments_.size(), sources_.size(), profiles_.size());
    return !species_.empty() && !environments_.empty();
}

const SpeciesDefinition* EvidenceDatabase::findSpecies(const std::string& id) const
{
    const int i = speciesIndex(id);
    return i >= 0 ? &species_[static_cast<size_t>(i)] : nullptr;
}

int EvidenceDatabase::speciesIndex(const std::string& id) const
{
    for (size_t i = 0; i < species_.size(); ++i)
    {
        if (species_[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const EnvironmentDefinition* EvidenceDatabase::findEnvironment(const std::string& id) const
{
    for (const EnvironmentDefinition& e : environments_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

const SourceRef* EvidenceDatabase::findSource(const std::string& id) const
{
    auto it = sources_.find(id);
    return it == sources_.end() ? nullptr : &it->second;
}

const ReconstructionProfile* EvidenceDatabase::findProfile(const std::string& id) const
{
    for (const ReconstructionProfile& p : profiles_)
    {
        if (p.id == id)
        {
            return &p;
        }
    }
    return nullptr;
}

float EvidenceDatabase::constant(const std::string& dottedPath, float fallback) const
{
    const JsonValue& v = simConstants_.path(dottedPath);
    if (v.isNumber())
    {
        return v.asFloat(fallback);
    }
    if (v.isObject() && v.get("value").isNumber())
    {
        return v.get("value").asFloat(fallback);
    }
    return fallback;
}

std::vector<int> EvidenceDatabase::speciesIndicesFor(const std::vector<std::string>& ids) const
{
    std::vector<int> out;
    for (const std::string& id : ids)
    {
        const int i = speciesIndex(id);
        if (i >= 0)
        {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<ValidationIssue> EvidenceDatabase::validate() const
{
    std::vector<ValidationIssue> issues = loadIssues_;
    auto add = [&](ValidationIssue::Severity s, const std::string& f, const std::string& m) { issues.push_back({s, f, m}); };

    auto checkClaimSources = [&](const std::string& file, const std::string& where, const Claim& c) {
        bool anyVerified = false;
        for (const std::string& sid : c.sources)
        {
            const SourceRef* src = findSource(sid);
            if (!src)
            {
                add(ValidationIssue::Severity::Error, file, where + ": unresolved source id '" + sid + "'");
                continue;
            }
            anyVerified = anyVerified || src->verified;
        }
        const bool strong = c.confidence == Confidence::Established || c.confidence == Confidence::StronglySupported;
        if (strong && c.sources.empty())
        {
            add(ValidationIssue::Severity::Error, file, where + ": confidence '" + confidenceKey(c.confidence) + "' without any source");
        }
        else if (strong && !anyVerified)
        {
            add(ValidationIssue::Severity::Error, file, where + ": confidence above 'inferred' backed only by unverified sources");
        }
        if (c.confidence == Confidence::Unknown && c.summary.empty())
        {
            add(ValidationIssue::Severity::Warning, file, where + ": unknown claim should state 'Information insuffisante — recherche nécessaire.'");
        }
    };

    for (const SpeciesDefinition& sp : species_)
    {
        for (const std::string& topic : requiredSpeciesTopics())
        {
            if (!sp.claim(topic))
            {
                add(ValidationIssue::Severity::Warning, sp.sourceFile, "missing science topic '" + topic + "'");
            }
        }
        for (const auto& c : sp.science)
        {
            checkClaimSources(sp.sourceFile, "science." + c.first, c.second);
        }
        for (const CompetingHypothesis& h : sp.competing)
        {
            for (const CompetingPosition& p : h.positions)
            {
                for (const std::string& sid : p.sources)
                {
                    if (!findSource(sid))
                    {
                        add(ValidationIssue::Severity::Error, sp.sourceFile, "competing_hypotheses: unresolved source id '" + sid + "'");
                    }
                }
            }
        }
        // sim_basis entries naming sources must resolve.
        for (const auto& b : sp.simBasis)
        {
            const std::string& basis = b.second;
            if (basis.rfind("game_assumption", 0) == 0 || basis.rfind("derived", 0) == 0)
            {
                continue;
            }
            size_t start = 0;
            while (start < basis.size())
            {
                size_t end = basis.find(';', start);
                std::string token = basis.substr(start, end == std::string::npos ? std::string::npos : end - start);
                while (!token.empty() && token.front() == ' ')
                {
                    token.erase(token.begin());
                }
                while (!token.empty() && token.back() == ' ')
                {
                    token.pop_back();
                }
                const bool looksLikeId = !token.empty() && token.find(' ') == std::string::npos;
                if (looksLikeId && !findSource(token))
                {
                    add(ValidationIssue::Severity::Warning, sp.sourceFile, "sim_basis." + b.first + ": '" + token + "' is not a known source id");
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
        }
        if (sp.sim.role == SimRole::Agent && sp.sim.locomotion.maxSpeedMs <= 0.0f && !sp.sim.locomotion.fly && !sp.sim.locomotion.swim)
        {
            add(ValidationIssue::Severity::Error, sp.sourceFile, "agent species without any locomotion speed");
        }
    }

    for (const EnvironmentDefinition& env : environments_)
    {
        for (const auto& c : env.science)
        {
            checkClaimSources(env.sourceFile, "science." + c.first, c.second);
        }
        for (const std::string& sid : env.sim.speciesIds)
        {
            if (!findSpecies(sid))
            {
                add(ValidationIssue::Severity::Error, env.sourceFile, "environment lists unknown species '" + sid + "'");
            }
        }
    }

    for (const ReconstructionProfile& p : profiles_)
    {
        for (const ProfileToggle& t : p.toggles)
        {
            for (const std::string& sid : t.sources)
            {
                if (!findSource(sid))
                {
                    add(ValidationIssue::Severity::Error, "Profiles/" + p.id, "toggle " + t.id + ": unresolved source id '" + sid + "'");
                }
            }
        }
    }
    return issues;
}
} // namespace noctis
