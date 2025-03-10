#include <petsc.h>
#include <cmath>
#include <memory>
#include <vector>
#include "MpiTestFixture.hpp"
#include "boundarySolver/boundarySolver.hpp"
#include "domain/boxMesh.hpp"
#include "domain/modifiers/createLabel.hpp"
#include "domain/modifiers/distributeWithGhostCells.hpp"
#include "domain/modifiers/ghostBoundaryCells.hpp"
#include "domain/modifiers/mergeLabels.hpp"
#include "domain/modifiers/tagLabelBoundary.hpp"
#include "environment/runEnvironment.hpp"
#include "eos/transport/constant.hpp"
#include "finiteVolume/compressibleFlowFields.hpp"
#include "gtest/gtest.h"
#include "mathFunctions/functionFactory.hpp"
#include "mathFunctions/geom/sphere.hpp"
#include "utilities/mathUtilities.hpp"
#include "utilities/petscUtilities.hpp"

using namespace ablate;

typedef struct {
    testingResources::MpiTestParameter mpiTestParameter;
    PetscInt dim;
    std::string fieldAFunction;
    std::string fieldBFunction;
    std::string auxAFunction;
    std::string auxBFunction;
    std::string expectedFieldAGradient;
    std::string expectedFieldBGradient;
    std::string expectedAuxAGradient;
    std::string expectedAuxBGradient;
} BoundarySolverDistributedTestParameters;

class BoundarySolverDistributedTestFixture : public testingResources::MpiTestFixture, public ::testing::WithParamInterface<BoundarySolverDistributedTestParameters> {
   public:
    void SetUp() override { SetMpiParameters(GetParam().mpiTestParameter); }
};

static void FillStencilValues(PetscInt loc, const PetscScalar* stencilValues[], std::vector<PetscScalar>& selectValues) {
    for (std::size_t i = 0; i < selectValues.size(); i++) {
        selectValues[i] = stencilValues[i][loc];
    }
}

TEST_P(BoundarySolverDistributedTestFixture, ShouldComputeCorrectGradientsOnBoundary) {
    StartWithMPI
        // initialize petsc and mpi
        ablate::environment::RunEnvironment::Initialize(argc, argv);
        ablate::utilities::PetscUtilities::Initialize();

        // Define regions for this test
        auto insideRegion = std::make_shared<ablate::domain::Region>("insideRegion");
        auto boundaryFaceRegion = std::make_shared<ablate::domain::Region>("boundaryFaces");
        auto boundaryCellRegion = std::make_shared<ablate::domain::Region>("boundaryCells");
        auto fieldRegion = std::make_shared<ablate::domain::Region>("fieldRegion");

        // define a test field to compute gradients
        std::vector<std::shared_ptr<ablate::domain::FieldDescriptor>> fieldDescriptors = {
            std::make_shared<ablate::domain::FieldDescription>(
                "fieldA", "", ablate::domain::FieldDescription::ONECOMPONENT, ablate::domain::FieldLocation::SOL, ablate::domain::FieldType::FVM, fieldRegion),
            std::make_shared<ablate::domain::FieldDescription>(
                "fieldB", "", ablate::domain::FieldDescription::ONECOMPONENT, ablate::domain::FieldLocation::SOL, ablate::domain::FieldType::FVM, fieldRegion),
            std::make_shared<ablate::domain::FieldDescription>(
                "auxA", "", ablate::domain::FieldDescription::ONECOMPONENT, ablate::domain::FieldLocation::AUX, ablate::domain::FieldType::FVM, fieldRegion),
            std::make_shared<ablate::domain::FieldDescription>(
                "auxB", "", ablate::domain::FieldDescription::ONECOMPONENT, ablate::domain::FieldLocation::AUX, ablate::domain::FieldType::FVM, fieldRegion),
            std::make_shared<ablate::domain::FieldDescription>("resultGrad",
                                                               "",
                                                               std::vector<std::string>{"fieldAGrad" + ablate::domain::FieldDescription::DIMENSION,
                                                                                        "fieldBGrad" + ablate::domain::FieldDescription::DIMENSION,
                                                                                        "auxAGrad" + ablate::domain::FieldDescription::DIMENSION,
                                                                                        "auxBGrad" + ablate::domain::FieldDescription::DIMENSION},
                                                               ablate::domain::FieldLocation::SOL,
                                                               ablate::domain::FieldType::FVM,
                                                               fieldRegion)};

        auto dim = GetParam().dim;

        // define the test mesh and setup hthe labels
        auto mesh = std::make_shared<ablate::domain::BoxMesh>(
            "test",
            fieldDescriptors,
            std::vector<std::shared_ptr<ablate::domain::modifiers::Modifier>>{
                std::make_shared<domain::modifiers::DistributeWithGhostCells>(),
                std::make_shared<ablate::domain::modifiers::CreateLabel>(insideRegion, std::make_shared<ablate::mathFunctions::geom::Sphere>(std::vector<double>(dim, .5), .25)),
                std::make_shared<ablate::domain::modifiers::TagLabelBoundary>(insideRegion, boundaryFaceRegion, boundaryCellRegion),
                std::make_shared<ablate::domain::modifiers::MergeLabels>(fieldRegion, std::vector<std::shared_ptr<domain::Region>>{insideRegion, boundaryCellRegion}),
                std::make_shared<domain::modifiers::GhostBoundaryCells>()},
            std::vector<int>(dim, 5),
            std::vector<double>(dim, 0.0),
            std::vector<double>(dim, 1.0),
            std::vector<std::string>(dim, "NONE") /*boundary*/,
            true /*simplex*/);

        // create a boundarySolver
        auto boundarySolver =
            std::make_shared<boundarySolver::BoundarySolver>("testSolver", boundaryCellRegion, boundaryFaceRegion, std::vector<std::shared_ptr<boundarySolver::BoundaryProcess>>{}, nullptr, true);

        // Init the subDomain
        mesh->InitializeSubDomains({boundarySolver}, {});

        // Get the global vectors
        auto globVec = mesh->GetSolutionVector();

        // Initialize each of the fields
        auto subDomain = mesh->GetSubDomain(boundaryCellRegion);
        auto fieldFunctions = {
            std::make_shared<mathFunctions::FieldFunction>("fieldA", ablate::mathFunctions::Create(GetParam().fieldAFunction)),
            std::make_shared<mathFunctions::FieldFunction>("fieldB", ablate::mathFunctions::Create(GetParam().fieldBFunction)),
        };
        mesh->ProjectFieldFunctions(fieldFunctions, globVec);

        auto auxVec = subDomain->GetAuxVector();
        auto auxFieldFunctions = {
            std::make_shared<mathFunctions::FieldFunction>("auxA", ablate::mathFunctions::Create(GetParam().auxAFunction)),
            std::make_shared<mathFunctions::FieldFunction>("auxB", ablate::mathFunctions::Create(GetParam().auxBFunction)),
        };
        subDomain->ProjectFieldFunctionsToLocalVector(auxFieldFunctions, auxVec);

        // Set the boundary boundaryCells values so that they are the correct value on the centroid of the face
        boundarySolver->InsertFieldFunctions(fieldFunctions);
        boundarySolver->InsertFieldFunctions(auxFieldFunctions);

        // Test one boundary point at a time
        PetscReal activeCell[3];
        PetscInt totalDim;
        PetscDSGetTotalDimension(boundarySolver->GetSubDomain().GetDiscreteSystem(), &totalDim) >> utilities::PetscUtilities::checkError;

        // determine the cell size
        PetscReal stencilRadius = .5;

        // for each
        boundarySolver->RegisterFunction(
            [](PetscInt dim,
               const boundarySolver::BoundarySolver::BoundaryFVFaceGeom* fg,
               const PetscFVCellGeom* boundaryCell,
               const PetscInt uOff[],
               const PetscScalar* boundaryValues,
               const PetscScalar* stencilValues[],
               const PetscInt aOff[],
               const PetscScalar* auxValues,
               const PetscScalar* stencilAuxValues[],
               PetscInt stencilSize,
               const PetscInt stencil[],
               const PetscScalar stencilWeights[],
               const PetscInt sOff[],
               PetscScalar source[],
               void* ctx) {
                const PetscInt fieldA = 1;
                const PetscInt fieldB = 0;
                const PetscInt sourceField = 0;
                const PetscInt auxA = 1;
                const PetscInt auxB = 0;

                // check for the current active cell
                auto currentActiveCell = (PetscReal*)ctx;

                // Check to see if this is equal
                bool currentCell = true;
                for (PetscInt dir = 0; dir < dim; dir++) {
                    if (PetscAbsReal(boundaryCell->centroid[dir] - currentActiveCell[dir]) > 1E-8) {
                        currentCell = false;
                    }
                }
                if (!currentCell) {
                    return 0;
                }

                // Create a scratch space
                std::vector<PetscScalar> pointValues(stencilSize, 0.0);
                PetscInt sourceOffset = 0;

                // Compute each field
                FillStencilValues(uOff[fieldA], stencilValues, pointValues);
                boundarySolver::BoundarySolver::ComputeGradient(dim, boundaryValues[uOff[fieldA]], stencilSize, &pointValues[0], stencilWeights, source + sOff[sourceField] + (sourceOffset * dim));

                // Check this gradient with the dPhiDn Calc
                PetscScalar dPhiDNorm;
                boundarySolver::BoundarySolver::ComputeGradientAlongNormal(dim, fg, boundaryValues[uOff[fieldA]], stencilSize, &pointValues[0], stencilWeights, dPhiDNorm);
                if (PetscAbs(dPhiDNorm - (utilities::MathUtilities::DotVector(dim, source + sOff[sourceField] + (sourceOffset * dim), fg->normal))) > 1E-8) {
                    throw std::runtime_error("The ComputeGradientAlongNormal function computed a wrong gradient for boundaryValues[uOff[fieldA]]");
                }

                FillStencilValues(uOff[fieldB], stencilValues, pointValues);
                sourceOffset++;
                boundarySolver::BoundarySolver::ComputeGradient(dim, boundaryValues[uOff[fieldB]], stencilSize, &pointValues[0], stencilWeights, source + sOff[sourceField] + (sourceOffset * dim));
                boundarySolver::BoundarySolver::ComputeGradientAlongNormal(dim, fg, boundaryValues[uOff[fieldB]], stencilSize, &pointValues[0], stencilWeights, dPhiDNorm);
                if (PetscAbs(dPhiDNorm - (utilities::MathUtilities::DotVector(dim, source + sOff[sourceField] + (sourceOffset * dim), fg->normal))) > 1E-8) {
                    throw std::runtime_error("The ComputeGradientAlongNormal function computed a wrong gradient for boundaryValues[uOff[fieldB]]");
                }

                FillStencilValues(uOff[auxA], stencilAuxValues, pointValues);
                sourceOffset++;
                boundarySolver::BoundarySolver::ComputeGradient(dim, auxValues[aOff[auxA]], stencilSize, &pointValues[0], stencilWeights, source + sOff[sourceField] + (sourceOffset * dim));
                boundarySolver::BoundarySolver::ComputeGradientAlongNormal(dim, fg, auxValues[aOff[auxA]], stencilSize, &pointValues[0], stencilWeights, dPhiDNorm);
                if (PetscAbs(dPhiDNorm - (utilities::MathUtilities::DotVector(dim, source + sOff[sourceField] + (sourceOffset * dim), fg->normal))) > 1E-8) {
                    throw std::runtime_error("The ComputeGradientAlongNormal function computed a wrong gradient for auxValues[aOff[auxA]]");
                }

                FillStencilValues(uOff[auxB], stencilAuxValues, pointValues);
                sourceOffset++;
                boundarySolver::BoundarySolver::ComputeGradient(dim, auxValues[aOff[auxB]], stencilSize, &pointValues[0], stencilWeights, source + sOff[sourceField] + (sourceOffset * dim));
                boundarySolver::BoundarySolver::ComputeGradientAlongNormal(dim, fg, auxValues[aOff[auxB]], stencilSize, &pointValues[0], stencilWeights, dPhiDNorm);
                if (PetscAbs(dPhiDNorm - (utilities::MathUtilities::DotVector(dim, source + sOff[sourceField] + (sourceOffset * dim), fg->normal))) > 1E-8) {
                    throw std::runtime_error("The ComputeGradientAlongNormal function computed a wrong gradient for auxValues[aOff[auxB]]");
                }

                // The normal should point away from the center of the domain
                PetscScalar center[3] = {.5, .5, .5};
                PetscScalar outwardVector[3];
                for (PetscInt d = 0; d < dim; d++) {
                    outwardVector[d] = fg->centroid[d] - center[d];
                }
                if (utilities::MathUtilities::DotVector(dim, outwardVector, fg->normal) <= 0) {
                    throw std::runtime_error("The Normal should face out from the inside region.");
                }

                return 0;
            },
            activeCell,
            {"resultGrad"},
            {"fieldB", "fieldA"},
            {"auxB", "auxA"},
            ablate::boundarySolver::BoundarySolver::BoundarySourceType::Distributed);

        // Create a locFVector
        Vec gradVec;
        DMCreateLocalVector(subDomain->GetDM(), &gradVec) >> utilities::PetscUtilities::checkError;

        // Get raw access to the vector
        const PetscScalar* gradArray;
        VecGetArrayRead(gradVec, &gradArray) >> utilities::PetscUtilities::checkError;

        // Get the offset for field
        PetscInt resultGradOffset;
        PetscDSGetFieldOffset(boundarySolver->GetSubDomain().GetDiscreteSystem(), boundarySolver->GetSubDomain().GetField("resultGrad").subId, &resultGradOffset) >>
            utilities::PetscUtilities::checkError;

        // get the exactGrads
        auto expectedFieldAGradient = ablate::mathFunctions::Create(GetParam().expectedFieldAGradient);
        auto expectedFieldBGradient = ablate::mathFunctions::Create(GetParam().expectedFieldBGradient);
        auto expectedAuxAGradient = ablate::mathFunctions::Create(GetParam().expectedAuxAGradient);
        auto expectedAuxBGradient = ablate::mathFunctions::Create(GetParam().expectedAuxBGradient);

        // Get the list of cells not in the boundary region (i.e. gas phase)
        PetscInt depth;
        DMPlexGetDepth(subDomain->GetDM(), &depth) >> utilities::PetscUtilities::checkError;
        IS allCellIS;
        DMGetStratumIS(subDomain->GetDM(), "depth", depth, &allCellIS) >> utilities::PetscUtilities::checkError;

        // Get the inside cells
        IS insideCellIS;
        IS labelIS;
        DMLabel insideLabel;
        DMGetLabel(subDomain->GetDM(), insideRegion->GetName().c_str(), &insideLabel);
        DMLabelGetStratumIS(insideLabel, insideRegion->GetValue(), &labelIS) >> utilities::PetscUtilities::checkError;
        ISIntersect(allCellIS, labelIS, &insideCellIS) >> utilities::PetscUtilities::checkError;
        ISDestroy(&labelIS) >> utilities::PetscUtilities::checkError;

        // Get the range
        PetscInt insideCellStart, insideCellEnd;
        const PetscInt* insideCells;
        ISGetPointRange(insideCellIS, &insideCellStart, &insideCellEnd, &insideCells);

        // get the cell geometry
        Vec cellGeomVec;
        const PetscScalar* cellGeomArray;
        DM cellGeomDm;
        DMPlexGetDataFVM(subDomain->GetDM(), nullptr, &cellGeomVec, nullptr, nullptr) >> utilities::PetscUtilities::checkError;
        VecGetDM(cellGeomVec, &cellGeomDm) >> utilities::PetscUtilities::checkError;
        VecGetArrayRead(cellGeomVec, &cellGeomArray) >> utilities::PetscUtilities::checkError;

        // March over each cell
        ablate::domain::Range boundaryCellRange;
        boundarySolver->GetCellRange(boundaryCellRange);
        for (PetscInt c = boundaryCellRange.start; c < boundaryCellRange.end; ++c) {
            // if there is a cell array, use it, otherwise it is just c
            const PetscInt cell = boundaryCellRange.points ? boundaryCellRange.points[c] : c;

            // Get the exact location of the face
            const auto& face = boundarySolver->GetBoundaryGeometry(cell);

            PetscFVCellGeom* cellGeom;
            DMPlexPointLocalRead(cellGeomDm, cell, cellGeomArray, &cellGeom) >> utilities::PetscUtilities::checkError;

            // Set the current location
            PetscArraycpy(activeCell, cellGeom->centroid, dim);

            // Reset the grad vec
            VecZeroEntries(gradVec) >> utilities::PetscUtilities::checkError;

            // evaluate
            boundarySolver->ComputeRHSFunction(0.0, globVec, gradVec) >> utilities::PetscUtilities::checkError;

            // Make sure that there is no source terms in this boundary solver region
            for (PetscInt tc = boundaryCellRange.start; tc < boundaryCellRange.end; ++tc) {
                const PetscInt testCell = boundaryCellRange.points ? boundaryCellRange.points[tc] : tc;

                const PetscScalar* data;
                DMPlexPointLocalRead(boundarySolver->GetSubDomain().GetDM(), testCell, gradArray, &data) >> utilities::PetscUtilities::checkError;
                for (PetscInt i = 0; i < totalDim; i++) {
                    ASSERT_DOUBLE_EQ(0.0, data[i]) << "All at the sources should be zero in the boundarySolverRegion " << testCell;
                }
            }

            // store the sum of the gradA, gradB, etc
            std::vector<PetscReal> sumGradA(3, 0.0);
            std::vector<PetscReal> sumGradB(3, 0.0);
            std::vector<PetscReal> sumGradAuxA(3, 0.0);
            std::vector<PetscReal> sumGradAuxB(3, 0.0);

            // March over the field sources and add them up, they should be equal to the expected values
            for (PetscInt ic = insideCellStart; ic < insideCellEnd; ++ic) {
                const PetscInt testCell = insideCells ? insideCells[ic] : ic;

                const PetscScalar* data;
                DMPlexPointLocalRead(boundarySolver->GetSubDomain().GetDM(), testCell, gradArray, &data) >> utilities::PetscUtilities::checkError;

                // If any value is not zero, make sure it is in the stencilRadius
                bool hasValue = false;
                for (PetscInt i = 0; i < totalDim; i++) {
                    if (data[i] > 1E-8) {
                        hasValue = true;
                    }
                }
                if (hasValue) {
                    // Get the centroid
                    PetscReal centroid[3];
                    PetscReal volume;
                    DMPlexComputeCellGeometryFVM(boundarySolver->GetSubDomain().GetDM(), testCell, &volume, centroid, nullptr) >> utilities::PetscUtilities::checkError;

                    PetscReal distance = 0.0;
                    for (PetscInt d = 0; d < dim; d++) {
                        distance += PetscSqr(centroid[d] - face.front().geometry.centroid[d]);
                    }
                    ASSERT_LT(PetscSqrtReal(distance), stencilRadius) << "Source terms should only be within the stencilRadius";

                    // All the fluxes before the offset should be zero
                    for (PetscInt i = 0; i < resultGradOffset; i++) {
                        ASSERT_DOUBLE_EQ(0.0, data[i]) << "All values not in the 'resultGrad' field should be zero.  Not zero at cell " << cell;
                    }

                    // Now add up the contributions for each cell
                    PetscInt offset = resultGradOffset;

                    // March over each field
                    for (PetscInt d = 0; d < dim; d++) {
                        sumGradA[d] += data[offset++] * volume;
                    }
                    for (PetscInt d = 0; d < dim; d++) {
                        sumGradB[d] += data[offset++] * volume;
                    }
                    for (PetscInt d = 0; d < dim; d++) {
                        sumGradAuxA[d] += data[offset++] * volume;
                    }
                    for (PetscInt d = 0; d < dim; d++) {
                        sumGradAuxA[d] += data[offset++] * volume;
                    }
                }
            }

            // Compute the expected values
            std::vector<PetscReal> exactGradA(3);
            std::vector<PetscReal> exactGradB(3);
            std::vector<PetscReal> exactGradAuxA(3);
            std::vector<PetscReal> exactGradAuxB(3);
            expectedFieldAGradient->Eval(face.front().geometry.centroid, dim, 0.0, exactGradA);
            expectedFieldBGradient->Eval(face.front().geometry.centroid, dim, 0.0, exactGradB);
            expectedAuxAGradient->Eval(face.front().geometry.centroid, dim, 0.0, exactGradAuxA);
            expectedAuxBGradient->Eval(face.front().geometry.centroid, dim, 0.0, exactGradAuxB);

            // compare the results
            // March over each field
            const double absError = 1E-8;
            for (PetscInt d = 0; d < dim; d++) {
                ASSERT_NEAR(exactGradA[d], sumGradA[d], absError) << "Expected gradient not found for FieldA dir " << d << " in cell " << cell;
            }
            for (PetscInt d = 0; d < dim; d++) {
                ASSERT_NEAR(exactGradB[d], sumGradB[d], absError) << "Expected gradient not found for FieldB dir " << d << " in cell " << cell;
            }
            for (PetscInt d = 0; d < dim; d++) {
                ASSERT_NEAR(exactGradAuxA[d], exactGradAuxA[d], absError) << "Expected gradient not found for AuxA dir " << d << " in cell " << cell;
            }
            for (PetscInt d = 0; d < dim; d++) {
                ASSERT_NEAR(exactGradAuxB[d], exactGradAuxB[d], absError) << "Expected gradient not found for AuxB dir " << d << " in cell " << cell;
            }
        }

        boundarySolver->RestoreRange(boundaryCellRange);
        VecRestoreArrayRead(gradVec, &gradArray) >> utilities::PetscUtilities::checkError;

        ISRestorePointRange(insideCellIS, &insideCellStart, &insideCellEnd, &insideCells);

        ISDestroy(&allCellIS);
        ISDestroy(&insideCellIS);
        VecRestoreArrayRead(cellGeomVec, &cellGeomArray) >> utilities::PetscUtilities::checkError;

        // debug code
        DMViewFromOptions(mesh->GetDM(), nullptr, "-viewTestDM");
        DMViewFromOptions(mesh->GetDM(), nullptr, "-viewTestDMAlso");

        VecDestroy(&gradVec) >> utilities::PetscUtilities::checkError;

        ablate::environment::RunEnvironment::Finalize();
        exit(0);
    EndWithMPI
}

INSTANTIATE_TEST_SUITE_P(BoundarySolver, BoundarySolverDistributedTestFixture,
                         testing::Values(
                             (BoundarySolverDistributedTestParameters){
                                 .mpiTestParameter = testingResources::MpiTestParameter("1D BoundarySolver"),
                                 .dim = 1,
                                 .fieldAFunction = "x + x*y+ y + z",
                                 .fieldBFunction = "10*x + 3*y + z*x +2*z",
                                 .auxAFunction = "-x - y -z",
                                 .auxBFunction = "-x*y*z",
                                 .expectedFieldAGradient = "1 + y, x + 1, 1",
                                 .expectedFieldBGradient = "10+z, 3, x + 2",
                                 .expectedAuxAGradient = "-1, -1, -1",
                                 .expectedAuxBGradient = "-y*z, -x*z, -x*y",

                             },
                             (BoundarySolverDistributedTestParameters){
                                 .mpiTestParameter = testingResources::MpiTestParameter("2D BoundarySolver"),
                                 .dim = 2,
                                 .fieldAFunction = "x + y + z",
                                 .fieldBFunction = "10*x + 3*y +2*z",
                                 .auxAFunction = "-x - y -z",
                                 .auxBFunction = "-x-x",
                                 .expectedFieldAGradient = "1,  1, 1",
                                 .expectedFieldBGradient = "10, 3,  2",
                                 .expectedAuxAGradient = "-1, -1, -1",
                                 .expectedAuxBGradient = "-2,0, 0",

                             },
                             (BoundarySolverDistributedTestParameters){
                                 .mpiTestParameter = testingResources::MpiTestParameter("3D BoundarySolver"),
                                 .dim = 3,
                                 .fieldAFunction = "x + y + z",
                                 .fieldBFunction = "10*x + 3*y +2*z",
                                 .auxAFunction = "-x - y -z",
                                 .auxBFunction = "-x-x",
                                 .expectedFieldAGradient = "1,  1, 1",
                                 .expectedFieldBGradient = "10, 3,  2",
                                 .expectedAuxAGradient = "-1, -1, -1",
                                 .expectedAuxBGradient = "-2,0, 0",

                             }),
                         [](const testing::TestParamInfo<BoundarySolverDistributedTestParameters>& info) { return info.param.mpiTestParameter.getTestName(); });
