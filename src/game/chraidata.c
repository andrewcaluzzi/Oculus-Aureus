/*========================================================================
 * chraidata.c
 *
 * Global AI Data to be used by any Setup.
 *
 *========================================================================*/
/**************************************************************************
 *
 *  $REVISION: 1.0 $
 *  $DATE: 2022/02/24 20:31:28 $
 *  $SOURCE: /src/game/chraidata.c,v $
 *
 **************************************************************************/
#include <ultra64.h>
#include <bondgame.h>
#include <bondaicommands.h>
#include "chrobjdata.h"

#pragma region Private Members

/*private enum, only accessible from within this file */
static enum lbl
{
    lblZero,
    lblLoop,
    lblDone,
    lblNext,
    lblWaiting,
    lblAlarmActivated,
    lblStoppedMoving,
    lblSeesBond,
    lblKneel             = 11,
    lbl12,
    lblBuddyShot,
    lblCloneContinue,
    lblRunToObjective,
    lblNearMiss,
    lblDead,
    /*Big Gap? wonder why*/
    lblRunning           = 27,
    lblResetAndRunAgain,
    lblAbleToRun,
    lblSitRep,
    lblCheckDistance     = 36,
    lblRun               = 40,
    lblMaybeThrowGrenade = 43,
    lblCheckDistance2    = 45,
    lbl46,
    lblInvincibleCheck
};


/* global ai lists used for all levels */
/* data */

/*D:80037070 */
/**
    Try aiming at bond, otherwise do nothing
    @return No Return - AI List can only be changed by a 3rd party via SetChrAiList
 */
u8 m_AimAtBond[] = {
    TRYAimAtBond(lblLoop)
    YIELD_FOREVER(lblLoop)
    EndList()
};

/*D:8003707C */
/**
    Dead or Removed AI.
    Use when AI has no more to do (or use YIELD_FOREVER)
    @return No Return - AI List can only be changed by a 3rd party via SetChrAiList
*/
u8 m_DeadAI[] = {
    /*PRINT("dyeng")*/
    YIELD_FOREVER(lblDead)
    EndList()
};

/*D:80037084 */
/**
    Stand Guard and Kill Time or patrol (Not typicaly used for patrolling).
    While killing time, play Idle animations
    On detecting Bond, Send a clone OR Run to Bond and Attack.
    This AI List is used by nearly all guards either as default or as a result
    of detecting Bond or finishing their assigned behaivior.
    @return No Return - AI List can only be changed by a 3rd party via SetChrAiList
 */
u8 m_StandardGuard[] = {
    /*
        THIS is used for CALL functions that return to this script.
        This is the only example available in Global AI where a script will
        return to itself - therefor the only example of CALL too.
    */
    #define THIS GAILIST_STANDARD_GUARD

    DO(lblLoop)
        IFImDyingOrDead(lblDead)    /* guard died, safely end list */
        IFImOnPatrolOrStopped(lblStoppedMoving) /* guard has stopped moving, safe to continue */
        /*ELSE*/
        CONTINUE(lblLoop)


        Label(lblStoppedMoving)
            /*Jump table*/
            IFISeeBond(lblSeesBond)
            IFIWasShotRecently(lblBuddyShot)
            IFIHeardBondRecently(lblCloneContinue)
            IFBondMissedMe(lblNearMiss)
            IFISeeSomeoneShot(lblBuddyShot)
            IFISeeSomeoneDie(lblBuddyShot)
            IFPlayingAnimation(lblNext)        /*CONTNUE*/
            IFNewRandomGreaterThan(1, lblNext) /* 1/255 chance of playing new animation else continue */
            /*Default*/
            CALL( GAILIST_PLAY_IDLE_ANIMATION) /* play idle animation and return to list */

        Label(lblNext)

    LOOP(lblLoop)


    Label(lblNearMiss) /* bond shot near guard */
        IFNewRandomGreaterThan(127, lblSeesBond) /* 50% chance of playing looking around animation */
        CALL( GAILIST_STARTLE_AND_RUN_TO_BOND)

    Label(lblSeesBond) /* guard sees bond */
        CALL( GAILIST_ATTACK_BOND)

    Label(lblBuddyShot) /* guard saw someone shot/die or guard was shot themselves */
        CALL( GAILIST_RUN_TO_BOND)

    Label(lblCloneContinue) /* guard heard bond, attempt to spawn clone (only if chr has clone flag) */
        SetReturnAiList(THIS) // This command is useless
        JumpTo(GAILIST_TRY_CLONE_SEND_OR_RUN_TO_BOND)

    Label(lblDead) /* guard has died, end routine */
        /*PRINT("DIE INSCAN\n")*/
        JumpTo( GAILIST_DEAD_AI)
    EndList()

    /*Undefine THIS and redefine for every script that wants to CALL subroutines*/
    #undef THIS
};

/*D:800370DC */
/**
    Play one random idle animation
    @return to caller if called with CALL
    -or-
    return to List set by SetReturnAiList - If not set will crash
 */
u8 m_IdleAnimations[] = {
#if 0
    IFNewRandomGreaterThan(50, lblNext) /* generate annd compare random seed to see which animation to play */
    PlayAnimation(ANIM_yawning, 0, 193, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        IFRandomGreaterThan(100, lblNext)
        PlayAnimation(ANIM_swatting_flies, 0, 294, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        IFRandomGreaterThan(150, lblNext)
        PlayAnimation(ANIM_scratching_leg, 0, 183, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        IFRandomGreaterThan(200, lblNext)
        PlayAnimation(ANIM_scratching_butt, 0, 123, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        IFRandomGreaterThan(250, lblNext)
        PlayAnimation(ANIM_adjusting_crotch, 0, 56, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        PlayAnimation(ANIM_sneeze, 0, 137, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblDone)
#else
    SWITCH(SetNewRandom(),
        IFRandomGreaterThan, 250,
            PlayAnimation(ANIM_sneeze          , 0, 137, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        IFRandomGreaterThan,200,
            PlayAnimation(ANIM_adjusting_crotch, 0,  56, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        IFRandomGreaterThan,150,
            PlayAnimation(ANIM_scratching_butt , 0, 123, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        IFRandomGreaterThan,100,
            PlayAnimation(ANIM_scratching_leg  , 0, 183, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        IFRandomGreaterThan,50,
            PlayAnimation(ANIM_swatting_flies  , 0, 294, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        /*DEFAULT*/,,
            PlayAnimation(ANIM_yawning         , 0, 193, ANIM_IDLE_POSE_WHEN_COMPLETE | ANIM_PLAY_SFX, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        /*pad unused case slots to SWITCH's 49 parameters - IDO's cpp treated
          the omitted trailing arguments as empty, GCC requires them spelled
          out; explicit empties expand identically on both*/
        ,,,,,,,,,, ,,,,,,,,,, ,,,,,,,,,
    )
#endif
    Return()
    EndList()
};

/*D:8003713C */
/**
    Bash that Keyboard once with a random animation
    @return to caller if called with CALL
    -or-
    return to List set by SetReturnAiList - If not set will crash
 */
u8 m_BashKeyboard[] = {
    #if 0
    IFNewRandomGreaterThan(60, lblNext)
    PlayAnimation(ANIM_keyboard_right_hand1, 0, 69, 0x00, ANIM_DEFAULT_INTERPOLATION)
    GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        IFRandomGreaterThan(120, lblNext)
        PlayAnimation(ANIM_keyboard_right_hand2, 0, 74, 0x00, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        IFRandomGreaterThan(180, lblNext)
        PlayAnimation(ANIM_keyboard_left_hand, 0, 79, 0x00, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */

    Label(lblNext)
        PlayAnimation(ANIM_keyboard_right_hand_tapping, 0, 89, 0x00, ANIM_DEFAULT_INTERPOLATION)
        GotoNext(lblDone) /* jump to end, we're done */
    Label(lblDone)
        #endif
    SWITCH(SetNewRandom(),
        IFRandomGreaterThan,180,
            PlayAnimation(ANIM_keyboard_right_hand_tapping, 0, 89, 0x00, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        IFRandomGreaterThan,120,
            PlayAnimation(ANIM_keyboard_left_hand, 0, 79, 0x00, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        IFRandomGreaterThan,60,
            PlayAnimation(ANIM_keyboard_right_hand2, 0, 74, 0x00, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        /*DEFAULT*/,,
            PlayAnimation(ANIM_keyboard_right_hand1, 0, 69, 0x00, ANIM_DEFAULT_INTERPOLATION)
            BREAK,
        /*pad unused case slots to SWITCH's 49 parameters (see m_IdleAnimations)*/
        ,,,,,,,,,, ,,,,,,,,,, ,,,,,,,,,, ,,,,,
    )

    Return()
    EndList()};

/*D:8003717C */
/**
    Stand Guard Statically (No Clones, No animations) or patrol.
    On detecting Bond (sight/near-miss only), Act like a Standard Guard.
    @return to Standard Guard
 */
u8 m_SimpleGuardDeaf[] = {
    DO(lblLoop) /* wait for guard to stop moving before branching to next logic */
        IFImOnPatrolOrStopped(lblStoppedMoving)
        /*ELSE*/
        CONTINUE(lblLoop)

        Label(lblStoppedMoving)
            IFISeeBond(lblSeesBond)
            IFIWasShotRecently(lblBuddyShot)
            IFBondMissedMe(lblNearMiss)
            IFISeeSomeoneShot(lblBuddyShot)
            IFISeeSomeoneDie(lblBuddyShot)
    LOOP(lblLoop)


    Label(lblNearMiss) /* bond shot near guard */
        IFNewRandomGreaterThan(127, lblSeesBond) /* 50% chance of playing looking around animation */
        JUMPTO_THEN_GUARD( GAILIST_STARTLE_AND_RUN_TO_BOND)

    Label(lblSeesBond) /* guard sees bond */
        JUMPTO_THEN_GUARD( GAILIST_ATTACK_BOND)

    Label(lblBuddyShot) /* guard saw another guard shot/die or guard was shot */
        JUMPTO_THEN_GUARD( GAILIST_RUN_TO_BOND)
    EndList()
};

/*D:800371B4 */
/**
    Attack Bond once via 1 random animation
    @return to caller if called with CALL
    -or-
    return to List set by SetReturnAiList - If not set will crash
 */
u8 m_AttackBond[] = {
    #if 0 //Doesnt work any simpler
    IFMyFlags2Has(FLAGS2_DONT_POINT_AT_BOND, lblNext) /* if guard already pointed at bond */
    /*ELSE*/
        IFNewRandomGreaterThan(32, lblNext)           /* 12.5% chance of pointing to bond */
        PointAtBond()
        SetMyFlags2(FLAGS2_DONT_POINT_AT_BOND)        /* don't point again, thank you object permanence */
        Return()                                      /* guard pointed at bond, return to list */

    Label(lblNext)
        SetMyFlags2(FLAGS2_DONT_POINT_AT_BOND)        /* I am awayer of Bond, set flag so we don't point at bond */
        SWITCH(/*SetNewRandom()*/EMPTY,
               IFRandomGreaterThan,127,
                    IFNewRandomGreaterThan(127, lblKneel)
                    TRYFireAtBond(lblDone)
                    Label(lblKneel)
                       TRYFireAtBondKneeling,
               IFNewRandomGreaterThan, 64,
                   TRYFiringRun,
               IFNewRandomGreaterThan,50,
                   TRYFiringWalk,                     /* infinite ammo death sentence ;) */
               IFNewRandomGreaterThan,25,
                   TRYSideRunning,
               IFNewRandomGreaterThan,10,
                   TRYSidestepping,
               IFNewRandomGreaterThan,10,
                   TRYSideHopping,
               /*DEFAULT*/,,
                   TRYFiringRoll,
               ,,
                   TRYThrowingGrenade                 /* attempt to throw grenade, depends on chr->grenadeprob value */
           )
    #else
    IFMyFlags2Has(FLAGS2_DONT_POINT_AT_BOND, lblNext) /* if guard already pointed at bond */
    IFNewRandomGreaterThan(32, lblNext)               /* 12.5% chance of pointing to bond */
    PointAtBond()
    SetMyFlags2(FLAGS2_DONT_POINT_AT_BOND)            /* don't point again, thank you object permanence */
    Return()                                          /* guard pointed at bond, return to list */

    Label(lblNext)                                    /*Not a switch*/
        SetMyFlags2(FLAGS2_DONT_POINT_AT_BOND)        /* I am awayer of Bond, set flag so we don't point at bond */
        TRYThrowingGrenade(lblDone)                   /* attempt to throw grenade, depends on chr->grenadeprob value */

    Label(lblNext)
        IFNewRandomGreaterThan(10, lblNext)
        TRYFiringRoll(lblDone)

    Label(lblNext)
        IFNewRandomGreaterThan(10, lblNext)
        TRYSideHopping(lblDone)

    Label(lblNext)
        IFNewRandomGreaterThan(25, lblNext)
        TRYSidestepping(lblDone)

    Label(lblNext)
        IFNewRandomGreaterThan(50, lblNext)
        TRYSideRunning(lblDone)

    Label(lblNext)
        IFNewRandomGreaterThan(64, lblNext)
        TRYFiringWalk(lblDone)                        /* infinite ammo death sentence ;) */

    Label(lblNext)
        IFRandomGreaterThan(127, lblNext)
        TRYFiringRun(lblDone)

    Label(lblNext)
        IFNewRandomGreaterThan(127, lblKneel)
        TRYFireAtBond(lblDone)

    Label(lblKneel)
        TRYFireAtBondKneeling(lblDone)

    Label(lblDone)                                    /* guard did the thing, now go back home */
#endif

    Return()
    EndList()
};

/*D:8003720C */
/**
    Run to bond and fire if seen, otherwise wait.
    @return to caller if called with CALL
    -or-
    return to List set by SetReturnAiList - If not set will crash
 */
u8 m_RunToBond[] = {
    SetMyFlags2(FLAGS2_DONT_POINT_AT_BOND) /* guard is aware of bond, so don't point at him when first spotted */
    TRYRunToBond(lblLoop)                  /* goto loop if bond position is reachable */
    Return()                               /* if guard can't reach bond, return to ai list (read TRYRunToBond command info) */

    DO(lblLoop)
        IFImOnPatrolOrStopped(lblStoppedMoving)
        IFISeeBond(lblSeesBond)            /* executes every frame instead of "at ready" state */
    LOOP(lblLoop)

    Label(lblSeesBond)
        JumpTo( GAILIST_ATTACK_BOND)

    Label(lblStoppedMoving)
        Return()
        EndList()
};

/*D:80037224 */
/**
    If Calling Chr NOT been seen, Send Clone after Bond, otherwise Act like a
    Standard Guard
    @return to Standard Guard
 */
u8 m_TryCloneSendOrRunToBond[] = {
    SetMyFlags2(FLAGS2_DONT_POINT_AT_BOND)            /* I am aware of bond, so won't point at him */
    IFIveNotBeenSeen(lblCloneContinue)
    JumpTo( GAILIST_RUN_TO_BOND)

    Label(lblCloneContinue)
        IFMyCloneDoesNotExist(lblCloneContinue)       /* Ive not been seen, test for clone existance */
        JumpTo( GAILIST_STANDARD_GUARD)               /* we only want 1 clone */

    Label(lblCloneContinue)                           /* clone didnt exist, create one */
        TRYCloningMe(GAILIST_STANDARD_CLONE, lblNext) /* Assign AI to clone */
        JumpTo( GAILIST_RUN_TO_BOND)                  /* clone failed to spawn (not enough memory/guard doesn't have clone flag on),
                                                         Its all down to me now. */

    Label(lblNext)                                    /* if clone spawn was successful */
        JumpTo( GAILIST_STANDARD_GUARD)               /* I'll sit tight while the clone does all the work, giving the
                                                         illusion that the level is a crowded complex */
    EndList()
};

/*D:80037248 */
/**
    Run to bond then act like a Standard Guard
    @return to Standard Guard
 */
u8 m_StandardClone[] = {
    JUMPTO_THEN_GUARD( GAILIST_RUN_TO_BOND)
    EndList()
};

/*D:80037250 */
/**
    Stand Guard Statically (No Clones, No animations) or patrol (Typical Use of this type).
    On detecting Bond, Act like a Standard Guard.
    @return to Standard Guard
 */
u8 m_SimpleGuard[] = {
    /* Simple Detection Loop */
    DO(lblLoop)
        IFISeeBond(lblSeesBond)
        IFIWasShotRecently(lblBuddyShot)
        IFIHeardBondRecently(lblBuddyShot)
        IFBondMissedMe(lblBuddyShot)
        IFISeeSomeoneShot(lblBuddyShot)
        IFISeeSomeoneDie(lblBuddyShot)
    LOOP(lblLoop)

    Label(lblSeesBond)      /* guard sees bond */
        JUMPTO_THEN_GUARD( GAILIST_ATTACK_BOND)

    Label(lblBuddyShot)     /* guard saw another guard shot/die or guard was shot */
        JUMPTO_THEN_GUARD( GAILIST_RUN_TO_BOND)

    Label(lblCloneContinue) /* unused spawn clone reaction for hearing bond, likely made game too difficult/slow */
        JUMPTO_THEN_GUARD( GAILIST_TRY_CLONE_SEND_OR_RUN_TO_BOND)
    EndList()
};

/*D:80037280 */
/**
    Stand Guard Statically (No Clones, No animations) or patrol.
    On detecting Bond, Run to padpreset1 and activate alarm.
    Act like a Standard Guard thereafter
    @return to Standard Guard
 */
u8 m_SimpleGuardAlarmRaiser[] = {

    DO(lblLoop)
        IFImOnPatrolOrStopped(lblStoppedMoving)
        /*ELSE*/
        CONTINUE(lblLoop)                                       /* Only process this script if not already running */
        /********************
            StoppedMoving
        *********************/
        Label(lblStoppedMoving)                                 /* break loop if any condition is true */
        IFISeeBond(lblRunToObjective)
        IFMyNumCloseArghsGreaterThan(0, lblNearMiss)
        IFIHeardBondRecently(lblNearMiss)
        IFISeeSomeoneShot(lblNearMiss)
        IFISeeSomeoneDie(lblNearMiss)
    LOOP(lblLoop)

    /*************
       NearMiss
    **************/
    Label(lblNearMiss)                                          /* a guard in sight was killed/shot, heard bond or bond shot guard */
    IFMyNumArghsGreaterThan(0, lblRunToObjective)               /* doesn't make sense why the guard would be mildly curious about getting shot */
    LookSurprised()

    DO(lblWaiting)                                              /* wait for guard to stop moving before branching to next logic (triggered by look around animation) */
        IFImOnPatrolOrStopped(lblRunToObjective)
    LOOP(lblWaiting)

    /********
       RUN!
    *********/
    Label(lblRunToObjective)
    RunToPad(PAD_PRESET1)

    DO(lblNext)                                                 /* wait for guard to stop moving (reached destination/guard was shot) */
        IFImOnPatrolOrStopped(lblDone)
    LOOP(lblNext)

    /*********
       DONE
    **********/
    Label(lblDone)
    IFMyDistanceToPadGreaterThanMeter( 1, PAD_PRESET1, lblDone) /* if guard is more than 1 meter away from alarm, skip to attack ai list */
    TRYTriggeringAlarmAtPad(PAD_PRESET1, lblAlarmActivated)
    GotoNext(lblDone)                                           /* didn't activate alarm (alarm destroyed?) */

    DO(lblAlarmActivated)                                       /* wait for guard to finish activating alarm */
        IFImOnPatrolOrStopped(lblDone)
    LOOP(lblAlarmActivated)

    /*********
       DONE
    **********/
    Label(lblDone)
    JUMPTO_THEN_GUARD( GAILIST_RUN_TO_BOND)                     /* Come Here!... Meh, Im just doing my job */
    EndList()
};

/*D:800372D0 */
/**
    Startle character then Run To Bond
    @return to caller if called with CALL -or- return to List set by SetReturnAiList - If not set will crash
 */
u8 m_StartleAndRunToBond[] = {
    LookSurprised() /* trigger animation */
    DO(lblLoop)
        IFImOnPatrolOrStopped(lblDone) /* wait for chr to stop moving */
    LOOP(lblLoop)

    Label(lblDone) /* chr stopped moving */
        JumpTo( GAILIST_RUN_TO_BOND)
    EndList()
};

/*D:800372E0 */
/**
    Persistently chase Bond and Attack (halt randomly)
    @return No Return - AI List can only be changed by a 3rd party via SetChrAiList
 */
u8 m_RunToBondPersistent[] = {

    /*DO*/
    Label(lblRun)
        TRYRunToBond(lblRunning)                                               /* goto loop if bond position is reachable */
        /* was PRINT of "no go!\n" - pre-expanded by hand: the N64 Makefile
           pipes this file through the ConvertAIPRINT sed to inline the
           string bytes before compiling, and the PC build has no such pass.
           The sed ignores this already-converted form on N64. */
        AI_PRINT,'n','o',' ','g','o','!','\n','\0',                            /* guard can't reach bond */

        DO(lblRunning)
            IFICouldSeeBond(lblStoppedMoving)
            IFImOnPatrolOrStopped(lblDone)
        LOOP(lblRunning)

        DO(lblLoop)                                                            /*Check Hits and Stopped/Patrol State*/
            IFMyNumArghsLessThan(6, lblCheckDistance2)
            /*ELSE*/IFMychrflagsHas(CHRFLAG_INVINCIBLE, lblInvincibleCheck) /*BREAK*/

            Label(lblCheckDistance2)
            IFImOnPatrolOrStopped(lblStoppedMoving)
            /*// BUG - this causes guard to shuffle about uselessly if Bond & guard are within line of sight and over 20 meters */
            IFMyDistanceToBondGreaterThanMeter(20, lblNext)                    /* if guard is further than 20 meters away from bond, break */
        LOOP(lblLoop)

        Label(lblNext)
            CONTINUE(lblRun)

        Label(lblStoppedMoving)
            IFICouldSeeBond(lblNext)                                           /*If Not invincible, do Attack loop*/
            GotoNext(lblDone)                                                  /*Goto end of Attack Switch*/

        Label(lblCheckDistance)
            Yield()
            IFMyDistanceToBondLessThanMeter(5, lblNext) /*BREAK*/
            /* OR */ IFImOnPatrolOrStopped(lblNext)     /*BREAK*/
    LOOP(lblRun)

    Label(lblNext)
        IFMychrflagsHas(CHRFLAG_INVINCIBLE, lblInvincibleCheck)                /*Check Hits, Unset flags and Loop Attack Options*/

    /*DO*/
    Label(lblMaybeThrowGrenade)
        #if 0
            IFNewRandomGreaterThan(10, lblNext)
            TRYThrowingGrenade(lblDone) /* depends on chr->grenadeprob value */

        Label(lblNext)
            IFRandomGreaterThan(50, lblNext)
            TRYSidestepping(lblDone)

        Label(lblNext)
            IFRandomGreaterThan(60, lblNext)
            TRYFiringRoll(lblDone)

        Label(lblNext)
            IFRandomGreaterThan(80, lblNext)
            TRYFiringRoll(lblDone)

        Label(lblNext)
            IFRandomGreaterThan(100, lblNext)
            TRYFiringWalk(lblDone)

        Label(lblNext)
            IFRandomGreaterThan(150, lblNext)
            TRYFiringRun(lblDone)

        Label(lblNext)
            IFRandomGreaterThan(200, lblNext)
            TRYFireAtBond(lblDone)

        Label(lblNext)
            TRYFireAtBondKneeling(lblDone)

        Label(lblDone)
        #else
            SWITCH(SetNewRandom(),
                IFRandomGreaterThan,200,
                    TRYFireAtBondKneeling,
                IFRandomGreaterThan, 150,
                    TRYFireAtBond,
                IFRandomGreaterThan, 100,
                    TRYFiringRun,
                IFRandomGreaterThan,80,
                    TRYFiringWalk,
                IFRandomGreaterThan,60,
                    TRYFiringRoll,
                IFRandomGreaterThan,50,
                    TRYFiringRoll,
                IFRandomGreaterThan,10,
                    TRYSidestepping,
                /*DEFAULT*/,,
                    TRYThrowingGrenade,
                /*pad unused case slots to SWITCH's 49 parameters (see m_IdleAnimations)*/
                ,,,,,,,,,, ,,,,,,,,,, ,,,
            )/*lblDone*/
        #endif
            IFICouldSeeBond(lblNext)
            GotoNext(lblRunning)

        Label(lblRunning)
            Yield()
            IFNewRandomGreaterThan(160, lblNext)
            GotoNext(lblResetAndRunAgain)

        Label(lblNext)
            Yield()
            GotoFirst(lblLoop)                                                 /*Check Hits and Stopped/Patrol State*/

        Label(lblResetAndRunAgain)
            MyTimerStart()
            TRYRunToBond(lblAbleToRun)

        DO(lblAbleToRun)
            IFImOnPatrolOrStopped(lblNext)
            IFMyTimerGreaterThanSeconds(1, lblNext)
        LOOP(lblAbleToRun)

        Label(lblNext)
            Stop()
            AI_PRINT,'w','a','i','t','\n','\0', /* was PRINT of "wait\n" - pre-expanded, see m_RunToBondPersistent */
            UnsetMyFlags2(FLAGS2_04)
            IFNewRandomGreaterThan(160, lblNext)
            SetMyFlags2(FLAGS2_04)

        Label(lblNext)
            MyTimerStart()

        DO(lblSitRep)
            IFICouldSeeBond(lblNext)/*BREAK*/
            IFBondMissedMe(lblNext) /*BREAK*/
            Yield()
            IFMyTimerLessThanSeconds(10, lblWaiting)
            IFMyFlags2Has(FLAGS2_04, lblAlarmActivated)
            GotoFirst(lblRun)

            Label(lblAlarmActivated)
                GotoFirst(lblResetAndRunAgain)

            Label(lblWaiting)
        LOOP(lblSitRep)

        Label(lblNext)
            GotoFirst(lblLoop)                                                 /*Check Hits and Stopped/Patrol State*/


        Label(lblInvincibleCheck)                                              /*Check Hits, Unset flags and Loop Attack Options*/
            IFMyNumArghsLessThan(6, lblNext)
            UnsetMychrflags(CHRFLAG_INVINCIBLE)

        Label(lblNext)
    LOOP(lblMaybeThrowGrenade)
    EndList()
};

/*D : 800373D0 */
/**
    Wait for one second then return
    @return to caller if called with CALL -or- return to List set by SetReturnAiList - If not set will crash
 */
u8 m_WaitOneSecond[] = {
    MyTimerStart()
    DO(lblRunning)
        IFMyTimerGreaterThanSeconds(1, lblNext) /* wait one second */
    LOOP(lblRunning)

    Label(lblNext)
        Return()
        EndList()
};

/*D:800373E0*/
/**
    Exit level and set BG AI to nothing
    @return No Return - AI is Dead
 */
u8 m_EndLevel[] = {
    EndLevel()
    JumpTo( GAILIST_DEAD_AI)
    EndList()
};

/*D:800373E8*/
/**
    Draw TT33, Aim and fire.
    Act like a Standard Guard thereafter
    @return to Standard Guard
 */
u8 m_DrawPistolAndAttackBond[] = {
    SetMySpeedRating(40) /* fast boi watch out! */
    SetMyAccuracyRating(50)

    Label(lblNext)
        TRYFacingBond(lblNext)

    Label(lblNext)
        MyTimerStart()

    DO(lblRunning)
        IFMyTimerGreaterThanTicks(20, lblNext) /* wait 1/3 of a second */
    LOOP(lblRunning)

    Label(lblNext)
        PlayAnimation(ANIM_fire_standing_draw_one_handed_weapon_fast,
                      0,
                      20,
                      ANIM_LOOP_HOLD_LAST_FRAME,
                      ANIM_DEFAULT_INTERPOLATION)

    DO(lblResetAndRunAgain)
        IFImOnPatrolOrStopped(lblNext)
    LOOP(lblResetAndRunAgain)

    Label(lblNext)
        TRYGiveMeItem(PROP_CHRTT33, ITEM_TT33, 0, lblNext)

    Label(lblNext)
        TRYAimAtBond( lblNext)       /* aim... */

    Label(lblNext)
        TRYFireAtBondUpdate(lblNext) /* FIRE! */

    Label(lblNext)
        DO(lblSitRep)                /* loop until chr has stopped firing at bond */
            IFImOnPatrolOrStopped(lblNext)
        LOOP(lblSitRep)

    Label(lblNext)
        JUMPTO_THEN_GUARD( GAILIST_RUN_TO_BOND)
    EndList()
};

/*D:80037444*/
/**
    Remove Calling chr and set AI to nothing
    @return No Return - AI is Dead
 */
u8 m_RemoveSelf[] = {
    RemoveMeInstantly() /* remove self */
    JumpTo( GAILIST_DEAD_AI)
    EndList()
};

#ifndef TARGET_N64
/**
    PORT: co-op bot. Idle forever, so the vanilla interpreter never fights the
    bot brain (port/src/port_bot.c), which drives the chr's actions from C.
    @return No Return
 */
u8 m_CoopBot[] = {
    Label(lblZero)
    Yield()
    GotoFirst(lblZero)
    EndList()
};

/**
    PORT: arena chase. A copy of m_RunToBondPersistent with one change: the "running" loop ends when Bond is within 9 m
    (was: as soon as the chr COULD SEE Bond, at any range). In the open Colosseum a chr sees Bond from the gate, so the stock
    list stopped it, then re-issued the run every tick while Bond was over 20 m away (the known "shuffles uselessly" bug):
    the walk animation restarted each tick and the chr stood and spun until Bond came close.
    @return No Return
 */
u8 m_ArenaChase[] = {

    Label(lblRun)
        TRYRunToBond(lblRunning)
        AI_PRINT,'n','o',' ','g','o','!','\n','\0',

        DO(lblRunning)
            IFMyDistanceToBondLessThanMeter(9, lblNearMiss)
            IFImOnPatrolOrStopped(lblDone)
        LOOP(lblRunning)

        Label(lblNearMiss)                  /* within 9 m: stop the run once Bond is in sight (or the walk is over) */
            IFICouldSeeBond(lblStoppedMoving)
            IFImOnPatrolOrStopped(lblDone)
            Yield()
            GotoFirst(lblRunning)

        DO(lblLoop)
            IFMyNumArghsLessThan(6, lblCheckDistance2)
            IFMychrflagsHas(CHRFLAG_INVINCIBLE, lblInvincibleCheck)

            Label(lblCheckDistance2)
            IFImOnPatrolOrStopped(lblStoppedMoving)
            IFMyDistanceToBondGreaterThanMeter(12, lblNext)
        LOOP(lblLoop)

        Label(lblNext)
            CONTINUE(lblRun)

        Label(lblStoppedMoving)
            IFICouldSeeBond(lblNext)
            GotoNext(lblDone)

        Label(lblCheckDistance)
            Yield()
            IFMyDistanceToBondLessThanMeter(5, lblNext)
            IFImOnPatrolOrStopped(lblNext)
    LOOP(lblRun)

    Label(lblNext)
        IFMychrflagsHas(CHRFLAG_INVINCIBLE, lblInvincibleCheck)

    Label(lblMaybeThrowGrenade)
        SWITCH(SetNewRandom(),
            IFRandomGreaterThan,200,
                TRYFireAtBondKneeling,
            IFRandomGreaterThan, 150,
                TRYFireAtBond,
            IFRandomGreaterThan, 100,
                TRYFiringRun,
            IFRandomGreaterThan,80,
                TRYFiringWalk,
            IFRandomGreaterThan,60,
                TRYFiringRoll,
            IFRandomGreaterThan,50,
                TRYFiringRoll,
            IFRandomGreaterThan,10,
                TRYSidestepping,
            /*DEFAULT*/,,
                TRYThrowingGrenade,
            ,,,,,,,,,, ,,,,,,,,,, ,,,
        )
            IFICouldSeeBond(lblNext)
            GotoNext(lblRunning)

        Label(lblRunning)
            Yield()
            IFNewRandomGreaterThan(160, lblNext)
            GotoNext(lblResetAndRunAgain)

        Label(lblNext)
            Yield()
            GotoFirst(lblLoop)

        Label(lblResetAndRunAgain)
            MyTimerStart()
            TRYRunToBond(lblAbleToRun)

        DO(lblAbleToRun)
            IFImOnPatrolOrStopped(lblNext)
            IFMyTimerGreaterThanSeconds(1, lblNext)
        LOOP(lblAbleToRun)

        Label(lblNext)
            Stop()
            UnsetMyFlags2(FLAGS2_04)
            IFNewRandomGreaterThan(160, lblNext)
            SetMyFlags2(FLAGS2_04)

        Label(lblNext)
            MyTimerStart()

        DO(lblSitRep)
            IFICouldSeeBond(lblNext)
            IFBondMissedMe(lblNext)
            Yield()
            IFMyTimerLessThanSeconds(10, lblWaiting)
            IFMyFlags2Has(FLAGS2_04, lblAlarmActivated)
            GotoFirst(lblRun)

            Label(lblAlarmActivated)
                GotoFirst(lblResetAndRunAgain)

            Label(lblWaiting)
        LOOP(lblSitRep)

        Label(lblNext)
            GotoFirst(lblLoop)

        Label(lblInvincibleCheck)
            IFMyNumArghsLessThan(6, lblNext)
            UnsetMychrflags(CHRFLAG_INVINCIBLE)

        Label(lblNext)
    LOOP(lblMaybeThrowGrenade)
    EndList()
};

/**
    PORT: arena engage lists (drop A5). The chr fires as soon as Bond is in line of sight (IFICouldSeeBond: a stan ray, no range
    limit, exactly what the Dam bunker guards use, see ai_11 in UsetupdamZ.c) AND within the weapon's effective range (LONG 60 m,
    MID 35 m, SHORT 25 m), instead of running to 9 m first. Bursts alternate with advancing: the stock attack-run/attack-walk
    actions (fire while walking straight at Bond) or, for chase soldiers, a plain run between bursts. Nothing re-issues a move
    every tick: runs are re-plotted at most every 3 s (and the position target follows Bond in C, port_arenamode.c).
    @return No Return
 */
/* Labels of the arena engage lists (the ids must be unique within a list; jumps go FORWARD to the next label, GotoFirst back to the first). */
static enum aelbl
{
    aeTop = 60, aeSee, aeAdv, aeGoing, aeW0, aeW0done, aeW1, aeChk, aeBack, aeClose, aeFight, aeRunWait, aeW3, aeW3done, aeW4,
    aeRunDone, aePush, aePushing, aeW2, aePushed, aeF1, aeF2, aeF3, aeC1, aeC2, aeC3, aeC4
};

/* The stock close-combat mix (< 12 m): sidestep, roll, walk-and-fire, kneel, stand and fire. Success jumps to aeFight. */
#define ARENA_CLOSE_BLOCK \
    Label(aeClose) \
        SetNewRandom() \
        IFRandomGreaterThan(40, aeC1) \
        TRYSidestepping(aeFight) \
    Label(aeC1) \
        IFRandomGreaterThan(80, aeC2) \
        TRYFiringRoll(aeFight) \
    Label(aeC2) \
        IFRandomGreaterThan(120, aeC3) \
        TRYFiringWalk(aeRunWait) \
    Label(aeC3) \
        IFRandomGreaterThan(190, aeC4) \
        TRYFireAtBondKneeling(aeFight) \
    Label(aeC4) \
        TRYFireAtBond(aeFight)

/* Far burst, chase soldiers: run-and-gun, walk-and-gun, kneel or stand and fire, by a roll. The attack-run/walk actions
 * (aeRunWait) advance toward Bond in a straight line while firing; they need 10 m+ and 3 s since the last walk, else the roll falls through. */
#define ARENA_FAR_CHASE \
        SetNewRandom() \
        IFRandomGreaterThan(90, aeF1) \
        TRYFiringRun(aeRunWait) \
    Label(aeF1) \
        IFRandomGreaterThan(150, aeF2) \
        TRYFiringWalk(aeRunWait) \
    Label(aeF2) \
        IFRandomGreaterThan(200, aeF3) \
        TRYFireAtBondKneeling(aeFight) \
    Label(aeF3) \
        TRYFireAtBond(aeFight)

/* Far burst, rushers: always run and gun; stand and fire only while the run is not allowed yet. */
#define ARENA_FAR_RUSH \
        TRYFiringRun(aeRunWait) \
        TRYFireAtBond(aeFight)

/* PUSH_ROLL: chase soldiers advance (aePush) after a burst with this chance (a roll above it); rushers never do (255). */
#define ARENA_ENGAGE(RANGE_M, FARBLOCK, PUSH_ROLL) \
    MyTimerStart() \
    Label(aeTop) \
        Yield() \
        IFICouldSeeBond(aeSee) \
        GotoNext(aeAdv) \
    Label(aeSee) \
        IFMyDistanceToBondGreaterThanMeter(RANGE_M, aeAdv) \
        IFMyDistanceToBondLessThanMeter(12, aeClose) \
        FARBLOCK \
        GotoNext(aeFight) \
    ARENA_CLOSE_BLOCK \
    Label(aeFight)                  /* a standing burst is on: let it run 2 s (the stock lists never wait for the attack to end) */ \
        MyTimerStart() \
        DO(aeW0) \
            IFMyTimerGreaterThanSeconds(2, aeW0done) \
        LOOP(aeW0) \
    Label(aeW0done) \
        IFNewRandomGreaterThan(PUSH_ROLL, aePush) \
        GotoFirst(aeTop) \
    Label(aeRunWait)                /* an attack-run/walk is on: wait for it to end (it walks to Bond on its own), at most 9 s */ \
        MyTimerStart() \
        DO(aeW4) \
            IFImOnPatrolOrStopped(aeRunDone) \
            IFMyTimerGreaterThanSeconds(9, aeRunDone) \
        LOOP(aeW4) \
    Label(aeRunDone) \
        GotoFirst(aeTop) \
    Label(aeAdv)                    /* Bond not in sight, or beyond the effective range: path toward him */ \
        TRYRunToBond(aeGoing) \
        MyTimerStart()              /* no route: try again in a second, never every tick */ \
        DO(aeW3) \
            IFMyTimerGreaterThanSeconds(1, aeW3done) \
        LOOP(aeW3) \
    Label(aeW3done) \
        GotoFirst(aeTop) \
    Label(aeGoing)                  /* running: stop as soon as Bond is in range AND in sight; re-plot at most every 3 s */ \
        MyTimerStart() \
        DO(aeW1) \
            IFImOnPatrolOrStopped(aeBack) \
            IFMyTimerGreaterThanSeconds(3, aeBack) \
            IFMyDistanceToBondLessThanMeter(RANGE_M, aeChk) \
        LOOP(aeW1) \
    Label(aeChk) \
        IFICouldSeeBond(aeBack) \
        GotoFirst(aeW1) \
    Label(aeBack) \
        GotoFirst(aeTop) \
    Label(aePush)                   /* advance for up to 3 s (until 15 m) between bursts: never stand still forever */ \
        TRYRunToBond(aePushing) \
        GotoFirst(aeTop) \
    Label(aePushing) \
        MyTimerStart() \
        DO(aeW2) \
            IFMyDistanceToBondLessThanMeter(15, aePushed) \
            IFImOnPatrolOrStopped(aePushed) \
            IFMyTimerGreaterThanSeconds(3, aePushed) \
        LOOP(aeW2) \
    Label(aePushed) \
        GotoFirst(aeTop) \
    EndList()

u8 m_ArenaEngageLongChase[]  = { ARENA_ENGAGE(60, ARENA_FAR_CHASE, 150) };
u8 m_ArenaEngageMidChase[]   = { ARENA_ENGAGE(35, ARENA_FAR_CHASE, 150) };
u8 m_ArenaEngageShortChase[] = { ARENA_ENGAGE(25, ARENA_FAR_CHASE, 150) };
u8 m_ArenaEngageLongRush[]   = { ARENA_ENGAGE(60, ARENA_FAR_RUSH, 255) };
u8 m_ArenaEngageMidRush[]    = { ARENA_ENGAGE(35, ARENA_FAR_RUSH, 255) };
u8 m_ArenaEngageShortRush[]  = { ARENA_ENGAGE(25, ARENA_FAR_RUSH, 255) };

/**
    PORT: arena marksman. Holds position and, while Bond is in line of sight (at any range: IFICouldSeeBond ignores
    visionrange), fires bursts with TRYFireAtBond (the stock standing "fire at Bond" command m_StandardGuard / m_AttackBond
    use). If Bond has not been in sight for 4 s, the marksman runs toward Bond until he is in sight (or the run ends),
    then resumes holding and firing, so it is never stuck. (The stock GAILIST_AIM_AT_BOND is aim-only, so snipers tracked
    Bond but never shot.)
    @return No Return
 */
u8 m_ArenaSnipe[] = {
    SetMyAccuracyRating(50)                     /* far-range hit chance falls off as 300/dist; the rating compensates (as m_DrawPistolAndAttackBond) */
    MyTimerStart()                              /* timer = time since Bond was last in sight */

    DO(lblLoop)
        IFICouldSeeBond(lblStoppedMoving)
        IFMyTimerGreaterThanSeconds(4, lblDone)
    LOOP(lblLoop)

    Label(lblStoppedMoving)                     /* Bond in sight: (re)start a firing action, let it run 2 s */
        TRYFireAtBond(lblRunning)               /* fails only if the chr has no weapon */
        GotoFirst(lblLoop)

    Label(lblRunning)
        MyTimerStart()
        DO(lblNext)                             /* the stock lists never wait for the attack to end either: re-issue it on a timer */
            IFMyTimerGreaterThanSeconds(2, lblNearMiss)
        LOOP(lblNext)

    Label(lblNearMiss)
        MyTimerStart()                          /* sight timer restarts after each burst */
        GotoFirst(lblLoop)

    Label(lblDone)                              /* Bond lost for 4 s: relocate toward him until he is in sight */
        TRYRunToBond(lblRunning)
        MyTimerStart()                          /* no route: wait another 4 s */
        GotoFirst(lblLoop)

    Label(lblRunning)
        DO(lblNext)
            IFICouldSeeBond(lblWaiting)
            IFImOnPatrolOrStopped(lblWaiting)
        LOOP(lblNext)

    Label(lblWaiting)
        MyTimerStart()
        GotoFirst(lblLoop)
    EndList()
};
#endif

#pragma endregion Private Members

/*D:8003744C */
/* global ai lists (glists) */
AIListRecord g_GlobalAILists[] = {
    {m_AimAtBond               , GAILIST_AIM_AT_BOND},
    {m_DeadAI                  , GAILIST_DEAD_AI},
    {m_StandardGuard           , GAILIST_STANDARD_GUARD},
    {m_IdleAnimations          , GAILIST_PLAY_IDLE_ANIMATION},
    {m_BashKeyboard            , GAILIST_BASH_KEYBOARD},
    {m_SimpleGuardDeaf         , GAILIST_SIMPLE_GUARD_DEAF},
    {m_AttackBond              , GAILIST_ATTACK_BOND},
    {m_SimpleGuard             , GAILIST_SIMPLE_GUARD},
    {m_RunToBond               , GAILIST_RUN_TO_BOND},
    {m_SimpleGuardAlarmRaiser  , GAILIST_SIMPLE_GUARD_ALARM_RAISER},
    {m_StartleAndRunToBond     , GAILIST_STARTLE_AND_RUN_TO_BOND},
    {m_TryCloneSendOrRunToBond , GAILIST_TRY_CLONE_SEND_OR_RUN_TO_BOND},
    {m_StandardClone           , GAILIST_STANDARD_CLONE},
    {m_RunToBondPersistent     , GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND},
    {m_WaitOneSecond           , GAILIST_WAIT_ONE_SECOND},
    {m_EndLevel                , GAILIST_END_LEVEL},
    {m_DrawPistolAndAttackBond , GAILIST_DRAW_TT33_AND_ATTCK_BOND},
    {m_RemoveSelf              , GAILIST_REMOVE_CHR},
#ifndef TARGET_N64
    {m_CoopBot                 , GAILIST_COOP_BOT}, /* PORT: co-op bot idle list */
    {m_ArenaChase              , GAILIST_ARENA_CHASE},
    {m_ArenaSnipe              , GAILIST_ARENA_SNIPE},
    {m_ArenaEngageLongChase    , GAILIST_ARENA_ENGAGE_LONG_CHASE},
    {m_ArenaEngageMidChase     , GAILIST_ARENA_ENGAGE_MID_CHASE},
    {m_ArenaEngageShortChase   , GAILIST_ARENA_ENGAGE_SHORT_CHASE},
    {m_ArenaEngageLongRush     , GAILIST_ARENA_ENGAGE_LONG_RUSH},
    {m_ArenaEngageMidRush      , GAILIST_ARENA_ENGAGE_MID_RUSH},
    {m_ArenaEngageShortRush    , GAILIST_ARENA_ENGAGE_SHORT_RUSH},
#endif
    {NULL, 0}
};


/*D:800374E4 Also happens to be the same indices as levelID */
char *setup_text_pointers[] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "UsetupsevbunkerZ",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,"UsetupsiloZ",
    "UsetupsevbunkerZ","UsetupstatueZ","UsetupcontrolZ","UsetuparchZ","UsetuptraZ",
    "UsetupdestZ","UsetupsevbZ","UsetupaztZ","UsetuppeteZ","UsetupdepoZ","UsetuprefZ",
    "UsetupcrypZ","UsetupdamZ","UsetuparkZ","UsetuprunZ","UsetupsevxZ","UsetupjunZ",
    "UsetupdishZ","UsetupcaveZ","UsetupcatZ","UsetupcradZ","UsetupshoZ","UsetupsevxbZ",
    "UsetupeldZ","UsetupimpZ","UsetupashZ","UsetuplueZ","UsetupameZ","UsetupritZ",
    "UsetupoatZ","UsetupearZ","UsetupleeZ","UsetuplipZ","UsetuplenZ","UsetupwaxZ",
    "UsetuppamZ", NULL, NULL
};



