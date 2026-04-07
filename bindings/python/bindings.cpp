/**
 * pybind11 bindings for the shared basketball engine.
 *
 * Exposes StatNormalizer and GameEconomy to Python so that
 * scraper.py and NbaApi can use the canonical C++ implementations
 * instead of maintaining duplicate Python versions.
 *
 * Build: cmake -Dpybind11_DIR=$(python -m pybind11 --cmakedir) .. && make
 * Usage: import bball_py; cost = bball_py.calculate_draft_cost(0.25)
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "GameEconomy.h"
#include "ShotProbability.h"
#include "PlayerEntity.h"
#include "SynergyEngine.h"

namespace py = pybind11;

PYBIND11_MODULE(bball_py, m) {
    m.doc() = "Basketball engine core — shared C++ logic exposed to Python";

    // ── GameEconomy ──────────────────────────────────────────────────────────

    py::enum_<UnitCost>(m, "UnitCost")
        .value("ONE",   UnitCost::ONE)
        .value("TWO",   UnitCost::TWO)
        .value("THREE", UnitCost::THREE)
        .value("FOUR",  UnitCost::FOUR)
        .value("FIVE",  UnitCost::FIVE);

    py::class_<GameEconomy>(m, "GameEconomy")
        .def(py::init<>())
        .def("calculate_draft_cost", &GameEconomy::CalculateDraftCost,
             py::arg("player_salary"),
             "Map salary to 1-5 gold cost tier");

    py::class_<StatNormalizer>(m, "StatNormalizer")
        .def(py::init<>())
        .def("convert_z_score", &StatNormalizer::ConvertZScoreToGameStat,
             py::arg("z_score"),
             "Convert z-score to game stat (50 base + 20 per sigma, clamped 1-99)");

    // ── PlayerStats ─────────────────────────────────────────────────────────

    py::class_<PlayerStats>(m, "PlayerStats")
        .def(py::init<>())
        .def_readwrite("shooting", &PlayerStats::shooting)
        .def_readwrite("defense", &PlayerStats::defense)
        .def_readwrite("speed", &PlayerStats::speed)
        .def_readwrite("height_inches", &PlayerStats::height_inches)
        .def_readwrite("weight_lbs", &PlayerStats::weight_lbs)
        .def_readwrite("stamina", &PlayerStats::stamina);

    // ── ShotProbability ─────────────────────────────────────────────────────

    m.def("calculate_shot_probability", &CalculateShotProbability,
          py::arg("shooter"), py::arg("nearest_defender"), py::arg("hoop_pos"),
          "Contest-aware shot probability: exponential decay by distance with defender penalty");
}
