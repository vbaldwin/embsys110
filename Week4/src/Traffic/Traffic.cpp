/*******************************************************************************
 * Copyright (C) Gallium Studio LLC. All rights reserved.
 *
 * This program is open source software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Alternatively, this program may be distributed and modified under the
 * terms of Gallium Studio LLC commercial licenses, which expressly supersede
 * the GNU General Public License and are specifically designed for licensees
 * interested in retaining the proprietary status of their code.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Contact information:
 * Website - https://www.galliumstudio.com
 * Source repository - https://github.com/galliumstudio
 * Email - admin@galliumstudio.com
 ******************************************************************************/

#include "app_hsmn.h"
#include "fw_log.h"
#include "fw_assert.h"
#include "TrafficInterface.h"
#include "LampInterface.h"
#include "Traffic.h"

FW_DEFINE_THIS_FILE("Traffic.cpp")

namespace APP {

#undef ADD_EVT
#define ADD_EVT(e_) #e_,

static char const * const timerEvtName[] = {
    "TRAFFIC_TIMER_EVT_START",
    TRAFFIC_TIMER_EVT
};

static char const * const internalEvtName[] = {
    "TRAFFIC_INTERNAL_EVT_START",
    TRAFFIC_INTERNAL_EVT
};

static char const * const interfaceEvtName[] = {
    "TRAFFIC_INTERFACE_EVT_START",
    TRAFFIC_INTERFACE_EVT
};

Traffic::Traffic() :
    Active((QStateHandler)&Traffic::InitialPseudoState, TRAFFIC, "TRAFFIC"),
    m_lampNS(LAMP_NS, "LAMP_NS"), m_lampEW(LAMP_EW, "LAMP_EW"),
    m_waitTimer(GetHsm().GetHsmn(), WAIT_TIMER),
	m_ewCarApproach{},
	m_nsCarApproach{},
	m_lastEWApproach{}
{
    SET_EVT_NAME(TRAFFIC);
}

QState Traffic::InitialPseudoState(Traffic * const me, QEvt const * const e) {
    (void)e;
    return Q_TRAN(&Traffic::Root);
}

QState Traffic::Root(Traffic * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            // Initialize regions.
            me->m_lampNS.Init(me);
            me->m_lampEW.Init(me);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_INIT_SIG: {
            EVENT(e);
            return Q_TRAN(&Traffic::Stopped);
        }
        case TRAFFIC_START_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new TrafficStartCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_STATE);
            Fw::Post(evt);
            return Q_HANDLED();
        }
    }
    return Q_SUPER(&QHsm::top);
}

QState Traffic::Stopped(Traffic * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case TRAFFIC_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new TrafficStopCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            return Q_HANDLED();
        }
        case TRAFFIC_START_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new TrafficStartCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            return Q_TRAN(&Traffic::Started);
        }
    }
    return Q_SUPER(&Traffic::Root);
}

QState Traffic::Started(Traffic * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_INIT_SIG: {
            return Q_TRAN(&Traffic::NSGo);
        }
        case TRAFFIC_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new TrafficStopCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            return Q_TRAN(&Traffic::Stopped);
        }
    }
    return Q_SUPER(&Traffic::Root);
}

QState Traffic::NSGo(Traffic *me, QEvt const *e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            Evt *evt = new LampRedReq(LAMP_EW, GET_HSMN());
            me->PostSync(evt);
            me->m_waitTimer.Start(DIRECTION_CHANGE_TIMEOUT_MS);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
			me->m_ewCarApproach = false;
            return Q_HANDLED();
        }
        case WAIT_TIMER: {
        	EVENT(e);
            me->m_waitTimer.Stop();
            Evt *evt = new LampGreenReq(LAMP_NS, GET_HSMN());
            me->PostSync(evt);
            return Q_TRAN(&Traffic::NSMinimumDuration);
        }
        case TRAFFIC_CAR_EW_REQ: {
            EVENT(e);
            me->m_ewCarApproach = true;
            return Q_HANDLED();
        }
    }
    return Q_SUPER(&Traffic::Started);
}

QState Traffic::NSMinimumDuration(Traffic * const me, QEvt const * const e)
{
	switch(e->sig) {
		case Q_ENTRY_SIG: {
			EVENT(e);
            me->m_waitTimer.Start(NS_MIN_TIMEOUT_MS);
			break;
		}
		case Q_EXIT_SIG: {
			EVENT(e);
			me->m_waitTimer.Stop();
			return Q_HANDLED();
		}
		case WAIT_TIMER: {
			EVENT(e);
			if(me->m_ewCarApproach) {
				return  Q_TRAN(&Traffic::NSSlow);
			}
			return Q_TRAN(&Traffic::NSContinuedTraffic);
		}
	}
	return Q_SUPER(&Traffic::NSGo);
}

QState Traffic::NSContinuedTraffic(Traffic * const me, QEvt const * const e)
{
	switch (e->sig) {
		case Q_ENTRY_SIG: {
			EVENT(e);
			return Q_HANDLED();
		}
		case Q_EXIT_SIG: {
			EVENT(e);
			return Q_HANDLED();
		}
		case TRAFFIC_CAR_EW_REQ: {
			return Q_TRAN(&Traffic::NSSlow);
		}
	}
	return Q_SUPER(&Traffic::NSGo);
}

QState Traffic::NSSlow(Traffic *me, QEvt const *e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            Evt *evt = new LampYellowReq(LAMP_NS, GET_HSMN());
            me->PostSync(evt);
            me->m_waitTimer.Start(NS_SLOW_TIMEOUT_MS);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_waitTimer.Stop();
            return Q_HANDLED();
        }
        case WAIT_TIMER: {
            EVENT(e);
            return Q_TRAN(&Traffic::EWGo);
        }
    }
    return Q_SUPER(&Traffic::Started);
}

QState Traffic::EWGo(Traffic *me, QEvt const *e) {
    switch (e->sig)
    {
        case Q_ENTRY_SIG: {
            EVENT(e);
            Evt *evt = new LampRedReq(LAMP_NS, GET_HSMN());
            me->PostSync(evt);
            me->m_waitTimer.Start(DIRECTION_CHANGE_TIMEOUT_MS);
            me->m_lastEWApproach = 0;
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
    		me->m_nsCarApproach = false;
            return Q_HANDLED();
        }
        case WAIT_TIMER: {
        	EVENT(e);
        	me->m_waitTimer.Stop();
            Evt *evt = new LampGreenReq(LAMP_EW, GET_HSMN());
            me->PostSync(evt);
        	return Q_TRAN(&Traffic::EWMinimumDuration);
        }
        case TRAFFIC_CAR_NS_REQ: {
            EVENT(e);
            me->m_nsCarApproach = true;
            return Q_HANDLED();
        }
        case TRAFFIC_CAR_EW_REQ: {
        	EVENT(e);
        	me->m_lastEWApproach = me->m_waitTimer.ctr();
        	return Q_HANDLED();
        }
    }
    return Q_SUPER(&Traffic::Started);
}

QState Traffic::EWMinimumDuration(Traffic * const me, QEvt const * const e)
{
	switch(e->sig) {
	case Q_ENTRY_SIG: {
		EVENT(e);
		me->m_waitTimer.Start(EW_MIN_TIMEOUT_MS);
		return Q_HANDLED();
	}
	case Q_EXIT_SIG: {
		EVENT(e);
		me->m_waitTimer.Stop();
		return Q_HANDLED();
	}
	case WAIT_TIMER: {
		EVENT(e);
		if(me->m_nsCarApproach) {
			return Q_TRAN(&Traffic::EWSlow);
		}
		return Q_TRAN(&Traffic::EWContinuedTraffic);
	}
	}
	return Q_SUPER(&Traffic::EWGo);
}

QState Traffic::EWContinuedTraffic(Traffic * const me, QEvt const * const e)
{
	switch (e->sig) {
		case Q_ENTRY_SIG: {
			EVENT(e);
			me->continuedGo(true);
			return Q_HANDLED();
		}
		case Q_EXIT_SIG: {
			EVENT(e);
			me->m_waitTimer.Stop();
			return Q_HANDLED();
		}
		case TRAFFIC_CAR_EW_REQ: {
			EVENT(e);
			me->continuedGo(false);
			return Q_HANDLED();
		}
		case WAIT_TIMER:
		case TRAFFIC_CAR_NS_REQ: {
			EVENT(e);
			return Q_TRAN(&Traffic::EWSlow);
		}
	}
	return Q_SUPER(&Traffic::EWGo);
}

QState Traffic::EWSlow(Traffic *me, QEvt const *e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            Evt *evt = new LampYellowReq(LAMP_EW, GET_HSMN());
            me->PostSync(evt);
            me->m_waitTimer.Start(EW_SLOW_TIMEOUT_MS);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_waitTimer.disarm();
            return Q_HANDLED();
        }
        case WAIT_TIMER: {
            EVENT(e);
            return Q_TRAN(&Traffic::NSGo);
        }
        case TRAFFIC_CAR_EW_REQ: {
        	EVENT(e);
        	me->m_ewCarApproach = true;
        	return Q_HANDLED();
        }
    }
    return Q_SUPER(&Traffic::Started);
}

void Traffic::continuedGo(const bool initial)
{
	if(initial) {
		const uint32_t waitTime = (m_lastEWApproach == 0 ? EW_MAX_TIME_MS - EW_MIN_TIMEOUT_MS  // 5 seconds
				                                         : EW_MAX_TIME_MS - m_lastEWApproach); // 15 seconds - N remaining seconds
		m_waitTimer.Start(waitTime);
	}
	else {
		m_waitTimer.Stop();
		m_waitTimer.Start(EW_MAX_TIME_MS);
	}
}

/*
QState Traffic::MyState(Traffic * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_INIT_SIG: {
            return Q_TRAN(&Traffic::SubState);
        }
    }
    return Q_SUPER(&Traffic::SuperState);
}
*/

} // namespace APP
