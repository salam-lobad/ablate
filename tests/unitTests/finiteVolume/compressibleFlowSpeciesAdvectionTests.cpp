#include <petsc.h>
#include <cmath>
#include <memory>
#include <vector>
#include "MpiTestFixture.hpp"
#include "PetscTestErrorChecker.hpp"
#include "convergenceTester.hpp"
#include "domain/boxMesh.hpp"
#include "domain/modifiers/distributeWithGhostCells.hpp"
#include "domain/modifiers/ghostBoundaryCells.hpp"
#include "environment/runEnvironment.hpp"
#include "eos/perfectGas.hpp"
#include "finiteVolume/boundaryConditions/essentialGhost.hpp"
#include "finiteVolume/boundaryConditions/ghost.hpp"
#include "finiteVolume/compressibleFlowFields.hpp"
#include "finiteVolume/compressibleFlowSolver.hpp"
#include "finiteVolume/fluxCalculator/ausm.hpp"
#include "gtest/gtest.h"
#include "mathFunctions/functionFactory.hpp"
#include "monitors/solutionErrorMonitor.hpp"
#include "parameters/mapParameters.hpp"
#include "utilities/petscUtilities.hpp"
using namespace ablate;

struct CompressibleFlowAdvectionTestParameters {
    testingResources::MpiTestParameter mpiTestParameter;
    PetscInt initialNx;
    int levels;
    std::shared_ptr<mathFunctions::MathFunction> eulerExact;
    std::shared_ptr<mathFunctions::MathFunction> densityYiExact;
    std::vector<PetscReal> expectedL2Convergence;
    std::vector<PetscReal> expectedLInfConvergence;
};

class CompressibleFlowAdvectionFixture : public testingResources::MpiTestFixture, public ::testing::WithParamInterface<CompressibleFlowAdvectionTestParameters> {
   public:
    void SetUp() override { SetMpiParameters(GetParam().mpiTestParameter); }
};

TEST_P(CompressibleFlowAdvectionFixture, ShouldConvergeToExactSolution) {
    StartWithMPI
        // initialize petsc and mpi
        ablate::environment::RunEnvironment::Initialize(argc, argv);
        ablate::utilities::PetscUtilities::Initialize();

        PetscInt initialNx = GetParam().initialNx;

        // keep track of history
        testingResources::ConvergenceTester l2History("l2");
        testingResources::ConvergenceTester lInfHistory("lInf");

        // March over each level
        for (PetscInt l = 0; l < GetParam().levels; l++) {
            // Create a mesh
            PetscInt nx1D = initialNx * PetscPowRealInt(2, l);

            PetscPrintf(PETSC_COMM_WORLD, "Running Calculation at Level %" PetscInt_FMT " (%" PetscInt_FMT "x%" PetscInt_FMT ")\n", l, nx1D, nx1D);

            // determine required fields for finite volume compressible flow

            auto eos = std::make_shared<ablate::eos::PerfectGas>(std::make_shared<ablate::parameters::MapParameters>(std::map<std::string, std::string>{{"gamma", "1.4"}, {"Rgas", "287"}}),
                                                                 std::vector<std::string>{"O2", "H2O", "N2"});

            std::vector<std::shared_ptr<ablate::domain::FieldDescriptor>> fieldDescriptors = {std::make_shared<ablate::finiteVolume::CompressibleFlowFields>(eos)};

            auto mesh = std::make_shared<ablate::domain::BoxMesh>("simpleMesh",
                                                                  fieldDescriptors,
                                                                  std::vector<std::shared_ptr<ablate::domain::modifiers::Modifier>>{std::make_shared<domain::modifiers::DistributeWithGhostCells>(),
                                                                                                                                    std::make_shared<domain::modifiers::GhostBoundaryCells>()},
                                                                  std::vector<int>{(int)nx1D, (int)nx1D},
                                                                  std::vector<double>{0.0, 0.0},
                                                                  std::vector<double>{.01, .01},
                                                                  std::vector<std::string>{} /*boundary*/,
                                                                  false /*simplex*/
            );

            // setup solutions from the exact params
            auto exactEulerSolution = std::make_shared<mathFunctions::FieldFunction>("euler", GetParam().eulerExact);
            auto yiExactSolution = std::make_shared<mathFunctions::FieldFunction>("densityYi", GetParam().densityYiExact);

            // create a time stepper
            auto timeStepper = ablate::solver::TimeStepper(mesh,
                                                           nullptr,
                                                           {},
                                                           std::make_shared<ablate::domain::Initializer>(exactEulerSolution, yiExactSolution),
                                                           std::vector<std::shared_ptr<mathFunctions::FieldFunction>>{exactEulerSolution, yiExactSolution});

            // setup a flow parameters
            auto parameters = std::make_shared<ablate::parameters::MapParameters>(std::map<std::string, std::string>{{"cfl", "0.25"}});

            auto boundaryConditions = std::vector<std::shared_ptr<finiteVolume::boundaryConditions::BoundaryCondition>>{
                std::make_shared<finiteVolume::boundaryConditions::EssentialGhost>("walls", std::vector<int>{1, 2, 3, 4}, exactEulerSolution),
                std::make_shared<finiteVolume::boundaryConditions::EssentialGhost>("walls", std::vector<int>{1, 2, 3, 4}, yiExactSolution)};

            auto flowObject = std::make_shared<ablate::finiteVolume::CompressibleFlowSolver>("testFlow",
                                                                                             ablate::domain::Region::ENTIREDOMAIN,
                                                                                             nullptr /*options*/,
                                                                                             eos,
                                                                                             parameters,
                                                                                             nullptr /*transportModel*/,
                                                                                             std::make_shared<ablate::finiteVolume::fluxCalculator::Ausm>(),
                                                                                             boundaryConditions /*boundary conditions*/);

            // run
            timeStepper.Register(flowObject);
            timeStepper.Solve();

            // Name the flow field
            PetscObjectSetName(((PetscObject)mesh->GetSolutionVector()), "Numerical Solution") >> testErrorChecker;

            // Get the L2 and LInf norms
            std::vector<PetscReal> l2Norm = ablate::monitors::SolutionErrorMonitor(ablate::monitors::SolutionErrorMonitor::Scope::COMPONENT, ablate::utilities::MathUtilities::Norm::L2_NORM)
                                                .ComputeError(timeStepper.GetTS(), timeStepper.GetTime(), mesh->GetSolutionVector());
            std::vector<PetscReal> lInfNorm = ablate::monitors::SolutionErrorMonitor(ablate::monitors::SolutionErrorMonitor::Scope::COMPONENT, ablate::utilities::MathUtilities::Norm::LINF)
                                                  .ComputeError(timeStepper.GetTS(), timeStepper.GetTime(), mesh->GetSolutionVector());

            // Store the residual into history
            const PetscReal h = 0.01 / nx1D;
            l2History.Record(h, l2Norm);
            lInfHistory.Record(h, lInfNorm);
        }

        std::string l2Message;
        if (!l2History.CompareConvergenceRate(GetParam().expectedL2Convergence, l2Message)) {
            FAIL() << l2Message;
        }

        std::string lInfMessage;
        if (!lInfHistory.CompareConvergenceRate(GetParam().expectedLInfConvergence, lInfMessage)) {
            FAIL() << lInfMessage;
        }

        ablate::environment::RunEnvironment::Finalize();
        exit(0);
    EndWithMPI
}

INSTANTIATE_TEST_SUITE_P(CompressibleFlow, CompressibleFlowAdvectionFixture,
                         testing::Values(
                             (CompressibleFlowAdvectionTestParameters){
                                 .mpiTestParameter = testingResources::MpiTestParameter("yi advection", 1,
                                                                                        "-dm_plex_separate_marker -ts_adapt_type none "
                                                                                        " -ts_max_steps 50 -ts_dt 5e-05  "),
                                 .initialNx = 5,
                                 .levels = 4,
                                 .eulerExact = ablate::mathFunctions::Create("2.0, 500000, 8.0, 0.0"),
                                 .densityYiExact = ablate::mathFunctions::Create("2*.2*(1 + sin(2*_pi*(x-4*t)/.01))/2, 2*.3*(1 + sin(2*_pi*(x-4*t)/.01))/2, 2*(1-.5*(1 + sin(2*_pi*(x-4*t)/.01))/2)"),
                                 .expectedL2Convergence = {NAN, NAN, NAN, NAN, 1, 1, 1},
                                 .expectedLInfConvergence = {NAN, NAN, NAN, NAN, 1, 1, 1}},
                             (CompressibleFlowAdvectionTestParameters){
                                 .mpiTestParameter = testingResources::MpiTestParameter("mpi yi advection", 2,
                                                                                        "-dm_plex_separate_marker -dm_distribute -ts_adapt_type none "
                                                                                        "-ts_max_steps 50 -ts_dt 5e-05  "),
                                 .initialNx = 5,
                                 .levels = 4,
                                 .eulerExact = ablate::mathFunctions::Create("2.0, 500000, 8.0, 0.0"),
                                 .densityYiExact = ablate::mathFunctions::Create("2*.2*(1 + sin(2*_pi*(x-4*t)/.01))/2, 2*.3*(1 + sin(2*_pi*(x-4*t)/.01))/2, 2*(1-.5*(1 + sin(2*_pi*(x-4*t)/.01))/2)"),
                                 .expectedL2Convergence = {NAN, NAN, NAN, NAN, 1, 1, 1},
                                 .expectedLInfConvergence = {NAN, NAN, NAN, NAN, 1, 1, 1}}),
                         [](const testing::TestParamInfo<CompressibleFlowAdvectionTestParameters> &info) { return info.param.mpiTestParameter.getTestName(); });
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
