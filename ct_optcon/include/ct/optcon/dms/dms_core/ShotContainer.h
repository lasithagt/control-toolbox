/***********************************************************************************
Copyright (c) 2017, Michael Neunert, Markus Giftthaler, Markus Stäuble, Diego Pardo,
Farbod Farshidian. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of ETH ZURICH nor the names of its contributors may be used
      to endorse or promote products derived from this software without specific
      prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL ETH ZURICH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************************/

#ifndef CT_OPTCON_DMS_CORE_SHOT_CONTAINER_H_
#define CT_OPTCON_DMS_CORE_SHOT_CONTAINER_H_

#include <cmath>
#include <functional>

#include <ct/core/core.h>
#include <ct/core/integration/SensitivityIntegratorCT.h>

#include <ct/optcon/costfunction/CostFunctionQuadratic.hpp>

#include <ct/optcon/dms/dms_core/DmsDimensions.h>
#include <ct/optcon/dms/dms_core/OptVectorDms.h>
#include <ct/optcon/dms/dms_core/ControllerDms.h>

namespace ct {
namespace optcon {

/**
 * @ingroup    DMS
 *
 * @brief      This class performs the state and the sensitivity integration on
 *             a shot
 *
 * @tparam     STATE_DIM    The state dimension
 * @tparam     CONTROL_DIM  The control dimension
 */
template <size_t STATE_DIM, size_t CONTROL_DIM>
class ShotContainer
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	typedef DmsDimensions<STATE_DIM, CONTROL_DIM> DIMENSIONS;

	typedef typename DIMENSIONS::state_vector_t state_vector_t;
	typedef typename DIMENSIONS::control_vector_t control_vector_t;
	typedef typename DIMENSIONS::state_vector_array_t state_vector_array_t;
	typedef typename DIMENSIONS::control_vector_array_t control_vector_array_t;
	typedef typename DIMENSIONS::time_array_t time_array_t;

	typedef typename DIMENSIONS::state_matrix_t state_matrix_t;
	typedef typename DIMENSIONS::state_control_matrix_t state_control_matrix_t;

	typedef typename DIMENSIONS::state_matrix_array_t state_matrix_array_t;
	typedef typename DIMENSIONS::state_control_matrix_array_t state_control_matrix_array_t;

	ShotContainer() = delete;

	/**
	 * @brief      Custom constructor
	 *
	 * @param[in]  controlledSystem  The nonlinear system
	 * @param[in]  linearSystem      The linearized system
	 * @param[in]  costFct           The costfunction
	 * @param[in]  w                 The optimization vector
	 * @param[in]  controlSpliner    The control input spliner
	 * @param[in]  timeGrid          The timegrid 
	 * @param[in]  shotNr            The shot number
	 * @param[in]  settings          The dms settings
	 */
	ShotContainer(
			std::shared_ptr<ct::core::ControlledSystem<STATE_DIM, CONTROL_DIM>> controlledSystem,
			std::shared_ptr<ct::core::LinearSystem<STATE_DIM, CONTROL_DIM>> linearSystem,
			std::shared_ptr<ct::optcon::CostFunctionQuadratic<STATE_DIM, CONTROL_DIM>> costFct,
			std::shared_ptr<OptVectorDms<STATE_DIM, CONTROL_DIM>> w,
			std::shared_ptr<SplinerBase<control_vector_t>> controlSpliner,
			std::shared_ptr<TimeGrid> timeGrid,
			size_t shotNr,
			DmsSettings settings
	):
		controlledSystem_(controlledSystem),
		linearSystem_(linearSystem),
		costFct_(costFct),
		w_(w),
		controlSpliner_(controlSpliner),
		timeGrid_(timeGrid),
		shotNr_(shotNr),
		settings_(settings),
		integrationCount_(0),
		costIntegrationCount_(0),
		sensIntegrationCount_(0),
		costSensIntegrationCount_(0),
		x_history_(state_vector_array_t(0)),
		t_history_(time_array_t(0)),
		cost_(0.0),
		costGradientSi_(state_vector_t::Zero()),
		costGradientQi_(control_vector_t::Zero()),
		costGradientQip1_(control_vector_t::Zero())
	{
		if(shotNr_ >= settings.N_) throw std::runtime_error("Dms Shot Integrator: shot index >= settings.N_ - check your settings.");

		switch(settings_.integrationType_)
		{
			case DmsSettings::EULER:
			{
				integratorCT_ = std::allocate_shared<ct::core::SensitivityIntegratorCT<STATE_DIM, CONTROL_DIM>, Eigen::aligned_allocator<ct::core::SensitivityIntegratorCT<STATE_DIM, CONTROL_DIM>>>
						(Eigen::aligned_allocator<ct::core::SensitivityIntegratorCT<STATE_DIM, CONTROL_DIM>>(), controlledSystem_, core::IntegrationTypeCT::EULER);
				break;
			}
			case DmsSettings::RK4:
			{
				integratorCT_ = std::allocate_shared<ct::core::SensitivityIntegratorCT<STATE_DIM, CONTROL_DIM>, Eigen::aligned_allocator<ct::core::SensitivityIntegratorCT<STATE_DIM, CONTROL_DIM>>>
						(Eigen::aligned_allocator<ct::core::SensitivityIntegratorCT<STATE_DIM, CONTROL_DIM>>(), controlledSystem_, core::IntegrationTypeCT::RK4);
				break;
			}
			case DmsSettings::RK5:
			{
				throw std::runtime_error("Currently we do not support adaptive integrators in dms");
			}
			
			default:
			{
				std::cerr << "... ERROR: unknown integration type. Exiting" << std::endl;
				exit(0);
			}
		}

		tStart_ = timeGrid_->getShotStartTime(shotNr_);
		double t_shot_end = timeGrid_->getShotEndTime(shotNr_);

		// +0.5 needed to avoid rounding errors from double to size_t
		nSteps_ = double(t_shot_end - tStart_) / double(settings_.dt_sim_) + 0.5;
		// std::cout << "shotNr_: " << shotNr_ << "\t nSteps: " << nSteps_ << std::endl;

		integratorCT_->setLinearSystem(linearSystem_);

		if(settings_.costEvaluationType_ == DmsSettings::FULL)
			integratorCT_->setCostFunction(costFct_);
	}

	/**
	 * @brief      Performs the state integration between the shots
	 */
	void integrateShot()
	{
		if((w_->getUpdateCount() != integrationCount_))
		{
			integrationCount_ = w_->getUpdateCount();
			ct::core::StateVector<STATE_DIM> initState = w_->getOptimizedState(shotNr_);
			integratorCT_->integrate(initState, tStart_, nSteps_, settings_.dt_sim_, x_history_, t_history_);
		}
	}

	void integrateCost()
	{
		if((w_->getUpdateCount() != costIntegrationCount_))
		{
			costIntegrationCount_ = w_->getUpdateCount();
			integrateShot();	
			cost_ = 0.0;
			integratorCT_->integrateCost(cost_, tStart_, nSteps_, settings_.dt_sim_);
		}
	}

	/**
	 * @brief      Performs the state and the sensitivity integration between the shots
	 */
	void integrateSensitivities()
	{
		if((w_->getUpdateCount() != sensIntegrationCount_))
		{
			sensIntegrationCount_ = w_->getUpdateCount();
			integrateShot();
			dXdSiBack_.setIdentity();
			dXdQiBack_.setZero();
			integratorCT_->linearize();
			integratorCT_->integrateSensitivityDX0(dXdSiBack_, tStart_, nSteps_, settings_.dt_sim_);
			integratorCT_->integrateSensitivityDU0(dXdQiBack_, tStart_, nSteps_, settings_.dt_sim_);

			if(settings_.splineType_ == DmsSettings::PIECEWISE_LINEAR)
			{
				dXdQip1Back_.setZero();
				integratorCT_->integrateSensitivityDUf(dXdQip1Back_, tStart_, nSteps_, settings_.dt_sim_);
			}
		}
	}

	void integrateCostSensitivities()
	{
		if((w_->getUpdateCount() != costSensIntegrationCount_))
		{
			costSensIntegrationCount_ = w_->getUpdateCount();
			integrateSensitivities();
			costGradientSi_.setZero();
			costGradientQi_.setZero();
			integratorCT_->integrateCostSensitivityDX0(costGradientSi_, tStart_, nSteps_, settings_.dt_sim_);
			integratorCT_->integrateCostSensitivityDU0(costGradientQi_, tStart_, nSteps_, settings_.dt_sim_);

			if(settings_.splineType_ == DmsSettings::PIECEWISE_LINEAR)
			{
				costGradientQip1_.setZero();
				integratorCT_->integrateCostSensitivityDUf(costGradientQip1_, tStart_, nSteps_, settings_.dt_sim_);
			}
		}
	}

	void reset()
	{
		integratorCT_->clearStates();
		integratorCT_->clearSensitivities();
		integratorCT_->clearLinearization();
	}

	/**
	 * @brief      Returns the integrated state
	 *
	 * @return     The integrated state
	 */
	const state_vector_t getStateIntegrated()
	{
		return x_history_.back();
	}

	/**
	 * @brief      Returns the end time of the integration	
	 *
	 * @return     The end time of the integration.
	 */
	const double getIntegrationTimeFinal()
	{
		return t_history_.back();
	}

	/**
	 * @brief      Returns the integrated ODE sensitivity with respect to the
	 *             discretized state s_i
	 *
	 * @return     The integrated sensitivity
	 */
	const state_matrix_t getdXdSiIntegrated()
	{
		return dXdSiBack_;
	}

	/**
	 * @brief      Returns the integrated ODE sensitivity with respect to the
	 *             discretized inputs q_i
	 *
	 * @return     The integrated sensitivity
	 */
	const state_control_matrix_t getdXdQiIntegrated()
	{
		return dXdQiBack_;
	}

	/**
	 * @brief      Returns the integrated ODE sensitivity with respect to the
	 *             discretized inputs q_{i+1}
	 *
	 * @return     The integrated sensitivity
	 */
	const state_control_matrix_t getdXdQip1Integrated()
	{
		return dXdQip1Back_;
	}

	/**
	 * @brief      Returns the integrated ODE sensitivity with respect to the
	 *             time segments h_i
	 *
	 * @return     The integrated sensitivity
	 */
	// const state_vector_t getdXdHiIntegrated()
	// {
	// 	return dXdHi_history_.back();
	// }

	/**
	 * @brief      Gets the full integrated state trajectory.
	 *
	 * @return     The integrated state trajectory
	 */
	const state_vector_array_t& getXHistory() const
	{		
		return x_history_;
	}

	/**
	 * @brief      Returns the control input trajectory used during the state integration
	 *
	 * @return     The control trajectory
	 */
	const control_vector_array_t& getUHistory()
	{
		u_history_.clear();
		for(size_t t = 0; t < t_history_.size(); ++t)
		{
			u_history_.push_back(controlSpliner_->evalSpline(t_history_[t], shotNr_));
		}
		return u_history_;
	}

	/**
	 * @brief      Returns the time trajectory used during the integration
	 *
	 * @return     The time trajectory
	 */
	const time_array_t& getTHistory() const
	{
		return t_history_;
	}

	/**
	 * @brief      Gets the cost integrated.
	 *
	 * @return     The integrated cost.
	 */
	const double getCostIntegrated() const
	{
		return cost_;
	}

	/**
	 * @brief      Returns the cost gradient with respect to s_i integrated over
	 *             the shot
	 *
	 * @return     The cost gradient
	 */
	const state_vector_t getdLdSiIntegrated() const
	{
		return costGradientSi_;
	}

	/**
	 * @brief      Returns the cost gradient with respect to q_i integrated over
	 *             the shot
	 *
	 * @return     The cost gradient
	 */
	const control_vector_t getdLdQiIntegrated() const
	{
		return costGradientQi_;
	}

	/**
	 * @brief      Returns to cost gradient with respect to q_{i+1} integrated
	 *             over the shot
	 *
	 * @return     The cost gradient
	 */
	const control_vector_t getdLdQip1Integrated() const 
	{
		return costGradientQip1_;
	}

	/**
	 * @brief      Returns to cost gradient with respect to h_i integrated over
	 *             the shot
	 *
	 * @return     The cost gradient
	 */
	// const double getdLdHiIntegrated() const
	// {
	// 	return costGradientHi_;
	// }

	/**
	 * @brief      Returns a pointer to the nonlinear dynamics used for this
	 *             shot
	 *
	 * @return     The pointer to the nonlinear dynamics
	 */
	std::shared_ptr<ct::core::ControlledSystem<STATE_DIM, CONTROL_DIM>> getControlledSystemPtr() {
		return controlledSystem_;
	}


private:
	std::shared_ptr<ct::core::ControlledSystem<STATE_DIM, CONTROL_DIM>> controlledSystem_;
	std::shared_ptr<ct::optcon::CostFunctionQuadratic<STATE_DIM, CONTROL_DIM>> costFct_;
	std::shared_ptr<ct::core::LinearSystem<STATE_DIM, CONTROL_DIM>> linearSystem_;
	std::shared_ptr<OptVectorDms<STATE_DIM, CONTROL_DIM>> w_;
	std::shared_ptr<SplinerBase<control_vector_t>> controlSpliner_;
	std::shared_ptr<TimeGrid> timeGrid_;

	const size_t shotNr_; 
	const DmsSettings settings_;

	size_t integrationCount_;
	size_t costIntegrationCount_;
	size_t sensIntegrationCount_;
	size_t costSensIntegrationCount_;

	// Integrated trajectories
	state_vector_array_t x_history_;
	control_vector_array_t u_history_;
	time_array_t t_history_;

	//Sensitivity Trajectories
	state_matrix_t dXdSiBack_;
	state_control_matrix_t dXdQiBack_;
	state_control_matrix_t dXdQip1Back_;

	//Cost and cost gradient
	double cost_;
	state_vector_t costGradientSi_;
	control_vector_t costGradientQi_;
	control_vector_t costGradientQip1_;

	std::shared_ptr<ct::core::SensitivityIntegratorCT<STATE_DIM, CONTROL_DIM>> integratorCT_;
	size_t nSteps_;
	double tStart_;
};

} // namespace optcon
} // namespace ct

#endif //CT_OPTCON_DMS_CORE_SHOT_CONTAINER_H_
