#include "stdafx.h"
#include "GameSpy_Available.h"

bool CGameSpy_Available::CheckAvailableServices(shared_str& resultstr)
{
#ifdef XR_PLATFORM_WEB
    // No GameSpy from a browser (and multiplayer is out of scope there). The
    // check below busy-waits for a network answer that never comes: ~4s of a
    // frozen tab while the main menu is created.
    resultstr = "! Online Services are not available in the browser build.";
    return false;
#endif
    GSIACResult result;
    GSIStartAvailableCheckA(GAMESPY_GAMENAME);

    while ((result = GSIAvailableCheckThink()) == GSIACWaiting)
        msleep(5);

    if (result != GSIACAvailable)
    {
        switch (result)
        {
        case GSIACUnavailable: { resultstr = "! Online Services for STALKER are no longer available.";
        }
        break;
        case GSIACTemporarilyUnavailable:
        {
            resultstr = "! Online Services for STALKER are temporarily down for maintenance.";
        }
        break;
        }
        return false;
    }
    else
    {
        resultstr = "Success";
    };
    return true;
};
