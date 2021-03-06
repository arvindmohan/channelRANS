/*---------------------------------------------------------------------------*\
RANS Channel flow solver for OpenFOAM, with mass flow rate based pressure gradient correction.

Copyright Information
    Copyright (C) 1991-2013 OpenCFD Ltd.
    Copyright (C) 2013 Arvind T. Mohan

License
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

Application
    channelRANS

Description
    Modified PISO solver for RANS simulations with constant mass flow rate (Ubar) enforced. 
    Pressure Gradient adjusted to keep user defined Ubar constant. In LES, this is 
    accomplished with channelFoam in OpenFOAM 2.1.x

Author
    Arvind T. Mohan
\*---------------------------------------------------------------------------*/

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //
#include "fvCFD.H"
#include "singlePhaseTransportModel.H"
#include "turbulenceModel.H"
#include "IFstream.H"
#include "OFstream.H"
#include "Random.H"


int main(int argc, char *argv[])
{
    #include "setRootCase.H"
    #include "createTime.H"
    #include "createMesh.H"
    #include "createFields.H"
    #include "readTransportProperties.H"
    #include "initContinuityErrs.H"
    #include "createGradP.H"

    // * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

    Info<< "\nStarting time loop\n" << endl;

    while (runTime.loop())
    {
        Info<< "Time = " << runTime.timeName() << nl << endl;

        #include "readPISOControls.H"
        #include "CourantNo.H"

        // Pressure-velocity PISO corrector
        
            // Momentum predictor

            fvVectorMatrix UEqn
            (
                fvm::ddt(U)
              + fvm::div(phi, U)
              + turbulence->divDevReff(U)
             ==
                 flowDirection*gradP
            );

          UEqn.relax();

            if (momentumPredictor)
            {
                solve(UEqn == -fvc::grad(p));
            }

            // --- PISO loop

            volScalarField rAU(1.0/UEqn.A());

            for (int corr=0; corr<nCorr; corr++)
            {

                U = rAU*UEqn.H();
                phi = (fvc::interpolate(U) & mesh.Sf())
                    + fvc::ddtPhiCorr(rAU, U, phi);

                adjustPhi(phi, U, p);

                // Non-orthogonal pressure corrector loop
                for (int nonOrth=0; nonOrth<=nNonOrthCorr; nonOrth++)
                {
                    // Pressure corrector

                    fvScalarMatrix pEqn
                    (
                        fvm::laplacian(rAU, p) == fvc::div(phi)
                    );

                    pEqn.setReference(pRefCell, pRefValue);

                    if
                    (
                        corr == nCorr-1
                     && nonOrth == nNonOrthCorr
                    )
                    {
                        pEqn.solve(mesh.solver("pFinal"));
                    }
                    else
                    {
                        pEqn.solve();
                    }

                    if (nonOrth == nNonOrthCorr)
                    {
                        phi -= pEqn.flux();
                    }
                }

                #include "continuityErrs.H"

                U -= rAU*fvc::grad(p);
                U.correctBoundaryConditions();
            }
        


        // Correct driving force for a constant mass flow rate

        // Extract the velocity in the flow direction
        dimensionedScalar magUbarStar =
            (flowDirection & U)().weightedAverage(mesh.V());

        // Calculate the pressure gradient increment needed to
        // adjust the average flow-rate to the correct value
        dimensionedScalar gragPplus =
            (magUbar - magUbarStar)/rAU.weightedAverage(mesh.V());

        U += flowDirection*rAU*gragPplus;

        gradP += gragPplus;

        Info<< "Uncorrected Ubar = " << magUbarStar.value() << tab
            << "pressure gradient = " << gradP.value() << endl;

        turbulence->correct();

        runTime.write();

        #include "writeGradP.H"

        Info<< "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
            << "  ClockTime = " << runTime.elapsedClockTime() << " s"
            << nl << endl;
    }

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
