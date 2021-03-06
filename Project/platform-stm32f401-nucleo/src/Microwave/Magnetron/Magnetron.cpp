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
#include "MagnetronInterface.h"
#include "Magnetron.h"
#include "GpioOutInterface.h"
#include "GpioOut.h"

FW_DEFINE_THIS_FILE("Magnetron.cpp")

namespace APP {

#undef ADD_EVT
#define ADD_EVT(e_) #e_,

static char const * const timerEvtName[] = {
    "MAGNETRON_TIMER_EVT_START",
    MAGNETRON_TIMER_EVT
};

static char const * const internalEvtName[] = {
    "MAGNETRON_INTERNAL_EVT_START",
    MAGNETRON_INTERNAL_EVT
};

static char const * const interfaceEvtName[] = {
    "MAGNETRON_INTERFACE_EVT_START",
    MAGNETRON_INTERFACE_EVT
};

Magnetron::Magnetron() :
    Active((QStateHandler)&Magnetron::InitialPseudoState, MAGNETRON, "MAGNETRON"),
    m_stateTimer(GetHsm().GetHsmn(), STATE_TIMER),
    m_magnetronTimer(GetHsm().GetHsmn(), MAGNETRON_TIMER),
    m_onTime{},
    m_offTime{},
    m_remainingTime{},
    m_powerLevel{},
    m_pipe{nullptr},
    m_history{nullptr}
    {
        SET_EVT_NAME(MAGNETRON);
    }

QState Magnetron::InitialPseudoState(Magnetron * const me, QEvt const * const e) {
    (void)e;
    return Q_TRAN(&Magnetron::Root);
}

QState Magnetron::Root(Magnetron * const me, QEvt const * const e) {
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
            EVENT(e);
            return Q_TRAN(&Magnetron::Stopped);
        }
        case MAGNETRON_START_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new MagnetronStartCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_STATE, GET_HSMN());
            Fw::Post(evt);
            return Q_HANDLED();
        }
        case MAGNETRON_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            me->GetHsm().SaveInSeq(req);
            return Q_TRAN(&Magnetron::Stopping);
        }
    }
    return Q_SUPER(&QHsm::top);
}

QState Magnetron::Stopped(Magnetron * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case MAGNETRON_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new MagnetronStopCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            return Q_HANDLED();
        }
        case MAGNETRON_START_REQ: {
            EVENT(e);
            MagnetronStartReq const &req = static_cast<MagnetronStartReq const &>(*e);
            me->GetHsm().SaveInSeq(req);
            return Q_TRAN(&Magnetron::Starting);
        }
    }
    return Q_SUPER(&Magnetron::Root);
}

QState Magnetron::Starting(Magnetron * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            uint32_t timeout = MagnetronStartReq::TIMEOUT_MS;
            FW_ASSERT(timeout > GpioOutStartReq::TIMEOUT_MS);
            me->m_stateTimer.Start(timeout);
            me->GetHsm().ResetOutSeq();
            Evt *evt = new GpioOutStartReq(GPIO_OUT, GET_HSMN(), GEN_SEQ());
            me->GetHsm().SaveOutSeq(*evt);
            Fw::Post(evt);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_stateTimer.Stop();
            me->GetHsm().ClearInSeq();
            return Q_HANDLED();
        }
        case GPIO_OUT_START_CFM: {
            EVENT(e);
            ErrorEvt const &cfm = ERROR_EVT_CAST(*e);
            bool allReceived;
            if (!me->GetHsm().HandleCfmRsp(cfm, allReceived)) {
                Evt *evt = new Failed(GET_HSMN(), cfm.GetError(), cfm.GetOrigin(), cfm.GetReason());
                me->PostSync(evt);
            } else if (allReceived) {
                Evt *evt = new Evt(DONE, GET_HSMN());
                me->PostSync(evt);
            }
            return Q_HANDLED();
        }
        case FAILED:
        case STATE_TIMER: {
            EVENT(e);
            Evt *evt;
            if (e->sig == FAILED) {
                ErrorEvt const &failed = ERROR_EVT_CAST(*e);
                evt = new MagnetronStartCfm(me->GetHsm().GetInHsmn(), GET_HSMN(), me->GetHsm().GetInSeq(),
                                            failed.GetError(), failed.GetOrigin(), failed.GetReason());
            } else {
                evt = new MagnetronStartCfm(me->GetHsm().GetInHsmn(), GET_HSMN(), me->GetHsm().GetInSeq(), ERROR_TIMEOUT, GET_HSMN());
            }
            Fw::Post(evt);
            return Q_TRAN(&Magnetron::Stopping);
        }
        case DONE: {
            EVENT(e);
            Evt *evt = new MagnetronStartCfm(me->GetHsm().GetInHsmn(), GET_HSMN(), me->GetHsm().GetInSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            return Q_TRAN(&Magnetron::Started);
        }
    }
    return Q_SUPER(&Magnetron::Root);
}

QState Magnetron::Stopping(Magnetron * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            uint32_t timeout = MagnetronStopReq::TIMEOUT_MS;
            FW_ASSERT(timeout > GpioOutStopReq::TIMEOUT_MS);
            me->m_stateTimer.Start(timeout);
            me->GetHsm().ResetOutSeq();
            Evt *evt = new GpioOutStopReq(GPIO_OUT, GET_HSMN(), GEN_SEQ());
            me->GetHsm().SaveOutSeq(*evt);
            Fw::Post(evt);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_stateTimer.Stop();
            me->GetHsm().ClearInSeq();
            me->GetHsm().Recall();
            return Q_HANDLED();
        }
        case MAGNETRON_STOP_REQ: {
            EVENT(e);
            me->GetHsm().Defer(e);
            return Q_HANDLED();
        }
        case GPIO_OUT_STOP_CFM: {
            EVENT(e);
            ErrorEvt const &cfm = ERROR_EVT_CAST(*e);
            bool allReceived;
            if (!me->GetHsm().HandleCfmRsp(cfm, allReceived)) {
                Evt *evt = new Failed(GET_HSMN(), cfm.GetError(), cfm.GetOrigin(), cfm.GetReason());
                Fw::Post(evt);
            } else if (allReceived) {
                Evt *evt = new Evt(DONE, GET_HSMN());
                me->PostSync(evt);
            }
            return Q_HANDLED();
        }
        case FAILED:
        case STATE_TIMER: {
            EVENT(e);
            FW_ASSERT(0);
            // Will not reach here.
            return Q_HANDLED();
        }
        case DONE: {
            EVENT(e);
            Evt *evt = new MagnetronStopCfm(me->GetHsm().GetInHsmn(), GET_HSMN(), me->GetHsm().GetInSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            return Q_TRAN(&Magnetron::Stopped);
        }
    }
    return Q_SUPER(&Magnetron::Root);
}

QState Magnetron::Started(Magnetron * const me, QEvt const * const e) {
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
            return Q_TRAN(&Magnetron::Off);
        }
        case MAGNETRON_OFF_REQ: {
            EVENT(e);
            me->m_magnetronTimer.Stop();
            return Q_TRAN(&Magnetron::Off);
        }
        case MAGNETRON_ON_REQ: {
            EVENT(e);
            //get data from pipe
            MagnetronOnReq const &req = static_cast<MagnetronOnReq const &>(*e);
            me->m_pipe = req.GetPipe();
            uint32_t count {me->m_pipe->Read(&me->m_powerLevel, 1)};
            if(0 == count) {
                LOG("Could not read from magnetron pipe\n");
                return Q_HANDLED();
            }

            if(me->m_powerLevel > MIN_POWER) {
                if(me->m_powerLevel < MAX_POWER) {
                    //calculate on/off times
                    me->m_onTime = static_cast<float>(me->m_powerLevel/10.0f) * APP::Magnetron::CYCLE_TIME_MS;
                    me->m_offTime = static_cast<uint32_t>(APP::Magnetron::CYCLE_TIME_MS) - me->m_onTime;
                    LOG("[p=%d],[on=%d],[off=%d]\n\r", me->m_powerLevel, me->m_onTime, me->m_offTime);

                    //start magnetron timer
                    me->m_magnetronTimer.Start(me->m_onTime);
                }
                Evt *evt = new GpioOutPatternReq(GPIO_OUT, GET_HSMN(), GEN_SEQ(), me->m_powerLevel - 1);
                Fw::Post(evt);
            }
            return Q_TRAN(&Magnetron::On);
        }
    }
    return Q_SUPER(&Magnetron::Root);
}

QState Magnetron::Off(Magnetron * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
    }
    return Q_SUPER(&Magnetron::Started);
}

QState Magnetron::On(Magnetron * const me, QEvt const * const e) {
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
            if(MIN_POWER == me->m_powerLevel) {
                return Q_TRAN(&Magnetron::NotRunning);
            }
            return Q_TRAN(&Magnetron::Running);
        }
        case MAGNETRON_PAUSE_REQ: {
            EVENT(e);
            if(MIN_POWER < me->m_powerLevel && me->m_powerLevel < MAX_POWER) {
                me->m_remainingTime = me->m_magnetronTimer.currCtr();
                me->m_magnetronTimer.Stop();
            }
            return Q_TRAN(&Magnetron::Paused);
        }
    }
    return Q_SUPER(&Magnetron::Started);
}

QState Magnetron::Running(Magnetron * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_history = &Magnetron::Running;
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            Evt *evt = new GpioOutOffReq(GPIO_OUT, GET_HSMN(), GEN_SEQ());
            Fw::Post(evt);
            return Q_HANDLED();
        }
        case MAGNETRON_TIMER: {
            EVENT(e);
            me->m_magnetronTimer.Start(me->m_offTime);
            return Q_TRAN(&Magnetron::NotRunning);
        }
    }
    return Q_SUPER(&Magnetron::On);
}

QState Magnetron::NotRunning(Magnetron * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_history = &Magnetron::NotRunning;
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case MAGNETRON_TIMER: {
            EVENT(e);
            me->m_magnetronTimer.Start(me->m_onTime);
            Evt *evt = new GpioOutPatternReq(GPIO_OUT, GET_HSMN(), GEN_SEQ(), me->m_powerLevel - 1);
            Fw::Post(evt);
            return Q_TRAN(&Magnetron::Running);
        }
    }
    return Q_SUPER(&Magnetron::On);
}

QState Magnetron::Paused(Magnetron * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case MAGNETRON_ON_REQ: {
            EVENT(e);
            if(MIN_POWER < me->m_powerLevel) {
                if(me->m_powerLevel < MAX_POWER) {
                    me->m_magnetronTimer.Start(me->m_remainingTime);
                }
                //read from pipe and discard results
                uint32_t discard{};
                me->m_pipe->Read(&discard, 1);

                if(&Magnetron::Running == me->m_history) {
                    Evt *evt = new GpioOutPatternReq(GPIO_OUT, GET_HSMN(), GEN_SEQ(), me->m_powerLevel - 1);
                    Fw::Post(evt);
                }
            }
            return Q_TRAN(me->m_history);
        }
    }
    return Q_SUPER(&Magnetron::Started);
}



/*
QState Magnetron::MyState(Magnetron * const me, QEvt const * const e) {
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
            return Q_TRAN(&Magnetron::SubState);
        }
    }
    return Q_SUPER(&Magnetron::SuperState);
}
*/

} // namespace APP
