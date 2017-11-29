/**********************************************************************************************************************
This file is part of the Control Toolbox (https://adrlab.bitbucket.io/ct), copyright by ETH Zurich, Google Inc.
Authors:  Michael Neunert, Markus Giftthaler, Markus Stäuble, Diego Pardo, Farbod Farshidian
Licensed under Apache2 license (see LICENSE file in main directory)
**********************************************************************************************************************/

using namespace ct;
using namespace ct::core;
using namespace ct::optcon;

#include "../../testSystems/LinearOscillator.h"

const size_t state_dim = 2;
const size_t control_dim = 1;

void printSolution(const ct::core::StateVectorArray<state_dim>& x,
    const ct::core::ControlVectorArray<control_dim>& u,
    const ct::core::FeedbackArray<state_dim, control_dim>& K)
{
    std::cout << "x array:" << std::endl;
    for (size_t j = 0; j < x.size(); j++)
        std::cout << x[j].transpose() << std::endl;

    std::cout << "u array:" << std::endl;
    for (size_t j = 0; j < u.size(); j++)
        std::cout << u[j].transpose() << std::endl;

    std::cout << "K array:" << std::endl;
    for (size_t j = 0; j < K.size(); j++)
        std::cout << K[j] << std::endl << std::endl;
}


//TEST(ConstrainedLQOCSolverTest, BoxConstraintsTest)
void boxConstraintsTest()
{
    const size_t N = 5;
    const double dt = 0.5;

    bool verbose = true;  // optional verbose output

    // create instances of HPIPM and an unconstrained Gauss-Newton Riccati solver
    std::shared_ptr<LQOCSolver<state_dim, control_dim>> hpipmSolver(new HPIPMInterface<state_dim, control_dim>);
    std::shared_ptr<LQOCSolver<state_dim, control_dim>> gnRiccatiSolver(new GNRiccatiSolver<state_dim, control_dim>);

    // create linear-quadratic optimal control problem containers
    std::shared_ptr<LQOCProblem<state_dim, control_dim>> lqocProblem1(new LQOCProblem<state_dim, control_dim>(N));
    std::shared_ptr<LQOCProblem<state_dim, control_dim>> lqocProblem2(new LQOCProblem<state_dim, control_dim>(N));


    // create a continuous-time example system and discretize it
    std::shared_ptr<core::LinearSystem<state_dim, control_dim>> exampleSystem(new example::LinearOscillatorLinear());
    core::SensitivityApproximation<state_dim, control_dim> discreteExampleSystem(
        dt, exampleSystem, core::SensitivityApproximationSettings::APPROXIMATION::MATRIX_EXPONENTIAL);

    // nominal control
    ct::core::ControlVector<control_dim> u0;
    u0.setConstant(0);

    // initial state
    ct::core::StateVector<state_dim> x0;
    x0 << 2.5, 0.0;

    // desired final state
    ct::core::StateVector<state_dim> xf;
    xf.setConstant(0.0);


    // define cost function matrices
    StateMatrix<state_dim> Q;
    Q.setIdentity();
    Q *= 2.0;
    ControlMatrix<control_dim> R;
    R.setIdentity();
    R *= 2 * 2.0;

    // create a cost function
    std::shared_ptr<CostFunctionQuadratic<state_dim, control_dim>> costFunction(
        new CostFunctionQuadraticSimple<state_dim, control_dim>(Q, R, xf, u0, xf, Q));


    // control box constraints
    ct::core::ControlVector<control_dim> u_lb, u_ub;
    u_lb.setConstant(-0.5);
    u_ub.setConstant(0.5);

    // state box constraints
    ct::core::StateVector<state_dim> x_lb, x_ub;
    x_lb.setConstant(-20.0);
    x_ub.setConstant(20.0);
    x_lb(0) = 1.7;

    // solution variables needed later
    ct::core::StateVectorArray<state_dim> xSol_hpipm;
    ct::core::ControlVectorArray<control_dim> uSol_hpipm;
    ct::core::FeedbackArray<state_dim, control_dim> KSol_hpipm;

    if (verbose)
    {
        std::cout << " ================================================== " << std::endl;
        std::cout << " TEST CASE 1: FULL BOX CONSTRAINTS ON CONTROL INPUT " << std::endl;
        std::cout << " ================================================== " << std::endl;
    }

    // initialize the optimal control problems for both solvers
    lqocProblem1->setFromTimeInvariantLinearQuadraticProblem(x0, u0, discreteExampleSystem, *costFunction, xf, dt);
    lqocProblem2->setFromTimeInvariantLinearQuadraticProblem(x0, u0, discreteExampleSystem, *costFunction, xf, dt);
    lqocProblem1->setControlBoxConstraints(u_lb, u_ub);
    lqocProblem2->setControlBoxConstraints(u_lb, u_ub);

    // check that constraint configuration is right
    //    ASSERT_TRUE(lqocProblem1->isConstrained());
    //    ASSERT_FALSE(lqocProblem1->isGeneralConstrained());
    //    ASSERT_FALSE(lqocProblem1->isStateBoxConstrained());
    //    ASSERT_TRUE(lqocProblem1->isControlBoxConstrained());

    // set and try to solve the problem for both solvers
    hpipmSolver->setProblem(lqocProblem1);
    hpipmSolver->solve();

    try
    {
        gnRiccatiSolver->setProblem(lqocProblem2);
        gnRiccatiSolver->solve();
        //        ASSERT(false); // should never reach to this point
    } catch (std::exception& e)
    {
        std::cout << "GNRiccatiSolver failed with exception " << e.what() << std::endl;
        //ASSERT(TRUE)
    }

    // retrieve solutions from hpipm
    xSol_hpipm = hpipmSolver->getSolutionState();
    uSol_hpipm = hpipmSolver->getSolutionControl();
    hpipmSolver->getFeedback(KSol_hpipm);

    // todo assert that state and control box constraints are met

    if (verbose)
        printSolution(xSol_hpipm, uSol_hpipm, KSol_hpipm);



    if (verbose)
    {
        std::cout << " ================================================== " << std::endl;
        std::cout << " TEST CASE 2: FULL BOX CONSTRAINTS ON STATE VECTOR  " << std::endl;
        std::cout << " ================================================== " << std::endl;
    }

    // initialize the optimal control problems for both solvers
    lqocProblem1->setZero();
    lqocProblem2->setZero();
    lqocProblem1->setFromTimeInvariantLinearQuadraticProblem(x0, u0, discreteExampleSystem, *costFunction, xf, dt);
    lqocProblem2->setFromTimeInvariantLinearQuadraticProblem(x0, u0, discreteExampleSystem, *costFunction, xf, dt);
    lqocProblem1->setStateBoxConstraints(x_lb, x_ub);
    lqocProblem2->setStateBoxConstraints(x_lb, x_ub);

    // check that constraint configuration is right
    //    ASSERT_TRUE(lqocProblem1->isConstrained());
    //    ASSERT_FALSE(lqocProblem1->isGeneralConstrained());
    //    ASSERT_TRUE(lqocProblem1->isStateBoxConstrained());
    //    ASSERT_FALSE(lqocProblem1->isControlBoxConstrained());

    // set and try to solve the problem for both solvers
    hpipmSolver->setProblem(lqocProblem1);
    hpipmSolver->solve();

    try
    {
        gnRiccatiSolver->setProblem(lqocProblem2);
        gnRiccatiSolver->solve();
        //        ASSERT(false); // should never reach to this point
    } catch (std::exception& e)
    {
        std::cout << "GNRiccatiSolver failed with exception " << e.what() << std::endl;
        //ASSERT(TRUE)
    }

    // retrieve solutions from hpipm
    xSol_hpipm = hpipmSolver->getSolutionState();
    uSol_hpipm = hpipmSolver->getSolutionControl();
    hpipmSolver->getFeedback(KSol_hpipm);

    // todo assert that state and control box constraints are met

    if (verbose)
        printSolution(xSol_hpipm, uSol_hpipm, KSol_hpipm);


    if (verbose)
       {
           std::cout << " ================================================== " << std::endl;
           std::cout << " TEST CASE 3: BOX CONSTRAINTS ON STATE AND CONTROL  " << std::endl;
           std::cout << " ================================================== " << std::endl;
       }

       // initialize the optimal control problems for both solvers
       lqocProblem1->setZero();
       lqocProblem2->setZero();
       lqocProblem1->setFromTimeInvariantLinearQuadraticProblem(x0, u0, discreteExampleSystem, *costFunction, xf, dt);
       lqocProblem2->setFromTimeInvariantLinearQuadraticProblem(x0, u0, discreteExampleSystem, *costFunction, xf, dt);
       lqocProblem1->setStateBoxConstraints(x_lb, x_ub);
       lqocProblem2->setStateBoxConstraints(x_lb, x_ub);
       lqocProblem1->setControlBoxConstraints(u_lb, u_ub);
       lqocProblem2->setControlBoxConstraints(u_lb, u_ub);

       // check that constraint configuration is right
       //    ASSERT_TRUE(lqocProblem1->isConstrained());
       //    ASSERT_FALSE(lqocProblem1->isGeneralConstrained());
       //    ASSERT_TRUE(lqocProblem1->isStateBoxConstrained());
       //    ASSERT_TRUE(lqocProblem1->isControlBoxConstrained());

       // set and try to solve the problem for both solvers
       hpipmSolver->setProblem(lqocProblem1);
       hpipmSolver->solve();

       try
       {
           gnRiccatiSolver->setProblem(lqocProblem2);
           gnRiccatiSolver->solve();
           //        ASSERT(false); // should never reach to this point
       } catch (std::exception& e)
       {
           std::cout << "GNRiccatiSolver failed with exception " << e.what() << std::endl;
           //ASSERT(TRUE)
       }

       // retrieve solutions from hpipm
       xSol_hpipm = hpipmSolver->getSolutionState();
       uSol_hpipm = hpipmSolver->getSolutionControl();
       hpipmSolver->getFeedback(KSol_hpipm);

       // todo assert that state and control box constraints are met

       if (verbose)
           printSolution(xSol_hpipm, uSol_hpipm, KSol_hpipm);
}
