/* -------------------------------------------------------------------------- */
/* Copyright 2002-2012, OpenNebula Project Leads (OpenNebula.org)             */
/*                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may    */
/* not use this file except in compliance with the License. You may obtain    */
/* a copy of the License at                                                   */
/*                                                                            */
/* http://www.apache.org/licenses/LICENSE-2.0                                 */
/*                                                                            */
/* Unless required by applicable law or agreed to in writing, software        */
/* distributed under the License is distributed on an "AS IS" BASIS,          */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
/* See the License for the specific language governing permissions and        */
/* limitations under the License.                                             */
/* -------------------------------------------------------------------------- */

#include "TransferManager.h"
#include "NebulaLog.h"

#include "Nebula.h"

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

const char * TransferManager::transfer_driver_name = "transfer_exe";

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

extern "C" void * tm_action_loop(void *arg)
{
    TransferManager *  tm;

    if ( arg == 0 )
    {
        return 0;
    }

    tm = static_cast<TransferManager *>(arg);

    NebulaLog::log("TrM",Log::INFO,"Transfer Manager started.");

    tm->am.loop(0,0);

    NebulaLog::log("TrM",Log::INFO,"Transfer Manager stopped.");

    return 0;
}

/* -------------------------------------------------------------------------- */

int TransferManager::start()
{
    int               rc;
    pthread_attr_t    pattr;

    rc = MadManager::start();

    if ( rc != 0 )
    {
        return -1;
    }

    NebulaLog::log("TrM",Log::INFO,"Starting Transfer Manager...");

    pthread_attr_init (&pattr);
    pthread_attr_setdetachstate (&pattr, PTHREAD_CREATE_JOINABLE);

    rc = pthread_create(&tm_thread,&pattr,tm_action_loop,(void *) this);

    return rc;
};

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::trigger(Actions action, int _vid)
{
    int *   vid;
    string  aname;

    vid = new int(_vid);

    switch (action)
    {
    case PROLOG:
        aname = "PROLOG";
        break;

    case PROLOG_MIGR:
        aname = "PROLOG_MIGR";
        break;

    case PROLOG_RESUME:
        aname = "PROLOG_RESUME";
        break;

    case EPILOG:
        aname = "EPILOG";
        break;

    case EPILOG_STOP:
        aname = "EPILOG_STOP";
        break;

    case EPILOG_DELETE:
        aname = "EPILOG_DELETE";
        break;

    case EPILOG_DELETE_PREVIOUS:
        aname = "EPILOG_DELETE_PREVIOUS";
        break;

    case CHECKPOINT:
        aname = "CHECKPOINT";
        break;

    case DRIVER_CANCEL:
        aname = "DRIVER_CANCEL";
        break;

    case FINALIZE:
        aname = ACTION_FINALIZE;
        break;

    default:
        delete vid;
        return;
    }

    am.trigger(aname,vid);
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::do_action(const string &action, void * arg)
{
    int vid;

    if (arg == 0)
    {
        return;
    }

    vid  = *(static_cast<int *>(arg));

    delete static_cast<int *>(arg);

    if (action == "PROLOG")
    {
        prolog_action(vid);
    }
    else if (action == "PROLOG_MIGR")
    {
        prolog_migr_action(vid);
    }
    else if (action == "PROLOG_RESUME")
    {
        prolog_resume_action(vid);
    }
    else if (action == "EPILOG")
    {
        epilog_action(vid);
    }
    else if (action == "EPILOG_STOP")
    {
        epilog_stop_action(vid);
    }
    else if (action == "EPILOG_DELETE")
    {
        epilog_delete_action(vid);
    }
    else if (action == "EPILOG_DELETE_PREVIOUS")
    {
        epilog_delete_previous_action(vid);
    }
    else if (action == "CHECKPOINT")
    {
        checkpoint_action(vid);
    }
    else if (action == "DRIVER_CANCEL")
    {
        driver_cancel_action(vid);
    }
    else if (action == ACTION_FINALIZE)
    {
        NebulaLog::log("TrM",Log::INFO,"Stopping Transfer Manager...");

        MadManager::stop();
    }
    else
    {
        ostringstream oss;
        oss << "Unknown action name: " << action;

        NebulaLog::log("TrM", Log::ERROR, oss);
    }
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::prolog_action(int vid)
{
    ofstream      xfr;
    ostringstream os;
    string        xfr_name;

    const VectorAttribute * disk;
    string source;
    string type;
    string clon;
    string files;
    string size;
    string format;
    string tm_mad, system_tm_mad;

    VirtualMachine * vm;
    Nebula&          nd = Nebula::instance();

    const TransferManagerDriver * tm_md;

    vector<const Attribute *> attrs;
    int                       num;

    int  context_result;

    // ------------------------------------------------------------------------
    // Setup & Transfer script
    // ------------------------------------------------------------------------

    system_tm_mad = nd.get_system_ds_tm_mad();

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    if (!vm->hasHistory())
    {
        goto error_history;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }

    xfr_name = vm->get_transfer_file() + ".prolog";
    xfr.open(xfr_name.c_str(), ios::out | ios::trunc);

    if (xfr.fail() == true)
    {
        goto error_file;
    }

    // ------------------------------------------------------------------------
    // Swap and image Commands
    // ------------------------------------------------------------------------

    num = vm->get_template_attribute("DISK",attrs);

    for (int i=0; i < num ;i++, source="", type="", clon="", tm_mad="")
    {
        disk = dynamic_cast<const VectorAttribute *>(attrs[i]);

        if ( disk == 0 )
        {
            continue;
        }

        type = disk->vector_value("TYPE");

        if ( type.empty() == false)
        {
            transform(type.begin(),type.end(),type.begin(),
                (int(*)(int))toupper);
        }

        if ( type == "SWAP" )
        {
            // -----------------------------------------------------------------
            // Generate a swap disk image
            // -----------------------------------------------------------------
            size = disk->vector_value("SIZE");

            if (size.empty()==true)
            {
                vm->log("TM",Log::WARNING,"No size in swap image, skipping");
                continue;
            }

            //MKSWAP tm_mad size host:remote_system_dir/disk.i
            xfr << "MKSWAP " 
                << system_tm_mad << " "
                << size   << " " 
                << vm->get_hostname() << ":"
                << vm->get_remote_system_dir() << "/disk." << i << endl;
        }
        else if ( type == "FS" )
        {
            // -----------------------------------------------------------------
            // Create a clean file system disk image
            // -----------------------------------------------------------------
            size   = disk->vector_value("SIZE");
            format = disk->vector_value("FORMAT");

            if ( size.empty() || format.empty())
            {
                vm->log("TM",Log::WARNING,"No size or format in plain FS image,"
                        " skipping");
                continue;
            }
            //MKIMAGE tm_mad size format host:remote_system_dir/disk.i
            xfr << "MKIMAGE " 
                << system_tm_mad << " "
                << size   << " " 
                << format << " "
                << vm->get_hostname() << ":" 
                << vm->get_remote_system_dir() << "/disk." << i << endl;
        }
        else
        {
            tm_mad = disk->vector_value("TM_MAD");

            if ( tm_mad.empty() )
            {
                goto error_tm_mad;
            }

            // -----------------------------------------------------------------
            // CLONE or LINK disk images
            // -----------------------------------------------------------------
            clon = disk->vector_value("CLONE");
            size = disk->vector_value("SIZE");

            if ( clon.empty() == true )
            {
                clon = "YES"; //Clone by default
            }
            else
            {
                transform(clon.begin(),clon.end(),clon.begin(),
                    (int(*)(int))toupper);
            }

            // <CLONE|LN> tm_mad fe:SOURCE host:remote_system_ds/disk.i size
            if (clon == "YES")
            {
                xfr << "CLONE ";
            }
            else
            {
                xfr << "LN ";
            }

            xfr << tm_mad << " ";

            // -----------------------------------------------------------------
            // Get the disk image, and set source URL
            // -----------------------------------------------------------------
            source = disk->vector_value("SOURCE");

            if ( source.empty() )
            {
                goto error_empty_disk;
            }

            if ( source.find(":") == string::npos ) //Regular file
            {
                xfr << nd.get_nebula_hostname() << ":" << source << " ";
            }
            else //TM Plugin specific protocol
            {
                xfr << source << " ";
            }
            
            xfr << vm->get_hostname() << ":" 
                << vm->get_remote_system_dir() << "/disk." << i;

            if (!size.empty()) //Add size for dev based disks
            {
                xfr << " " << size;
            }

            xfr << endl;
        }
    }

    // ------------------------------------------------------------------------
    // Generate context file (There are 0...num-1 disks, constext is disk.num)
    // ------------------------------------------------------------------------

    context_result = vm->generate_context(files);

    if ( context_result == -1 )
    {
        goto error_context;
    }

    if ( context_result )
    {
        //CONTEXT tm_mad files hostname:remote_system_dir/disk.i
        xfr << "CONTEXT " 
            << system_tm_mad << " "
            << vm->get_context_file() << " ";

        if (!files.empty())
        {
            xfr << files << " ";
        }

        xfr << vm->get_hostname() << ":" 
            << vm->get_remote_system_dir() << "/disk." << num 
            << endl;
    }

    xfr.close();

    tm_md->transfer(vid,xfr_name);

    vm->unlock();

    return;

error_context:
    os.str("");
    os << "prolog, could not write context file for VM " << vid;
    goto error_common;

error_history:
    os.str("");
    os << "prolog, VM " << vid << " has no history";
    goto error_common;

error_file:
    os.str("");
    os << "prolog, could not open file: " << xfr_name;
    goto error_common;

error_driver:
    os.str("");
    os << "prolog, error getting Transfer Manager driver.";
    goto error_common;

error_tm_mad:
    os.str("");
    os << "prolog, undefined TM_MAD for disk image in VM template";
    xfr.close();

error_empty_disk:
    os.str("");
    os << "prolog, undefined source disk image in VM template";
    xfr.close();

error_common:
    (nd.get_lcm())->trigger(LifeCycleManager::PROLOG_FAILURE,vid);
    vm->log("TM", Log::ERROR, os);

    vm->unlock();
    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::prolog_migr_action(int vid)
{
    ofstream        xfr;
    ostringstream   os;
    string          xfr_name;

    const VectorAttribute * disk;
    string tm_mad;
    string system_tm_mad;

    vector<const Attribute *> attrs;
    int                       num;

    VirtualMachine *    vm;
    Nebula&             nd = Nebula::instance();

    const TransferManagerDriver * tm_md;

    // ------------------------------------------------------------------------
    // Setup & Transfer script
    // ------------------------------------------------------------------------

    system_tm_mad = nd.get_system_ds_tm_mad();

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    if (!vm->hasHistory())
    {
        goto error_history;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }

    xfr_name = vm->get_transfer_file() + ".migrate";
    xfr.open(xfr_name.c_str(), ios::out | ios::trunc);

    if (xfr.fail() == true)
    {
        goto error_file;
    }

    // ------------------------------------------------------------------------
    // Move system directory and disks
    // ------------------------------------------------------------------------

    num = vm->get_template_attribute("DISK",attrs);

    for (int i=0 ; i < num ; i++, tm_mad="")
    {
        disk = dynamic_cast<const VectorAttribute *>(attrs[i]);

        if ( disk == 0 )
        {
            continue;
        }

        tm_mad = disk->vector_value("TM_MAD");

        if ( tm_mad.empty() )
        {
            continue;
        }

        //MV tm_mad prev_host:remote_system_dir/disk.i host:remote_system_dir/disk.i
        xfr << "MV "
            << tm_mad << " "
            << vm->get_previous_hostname() << ":" 
            << vm->get_remote_system_dir() << "/disk." << i << " "
            << vm->get_hostname() << ":"
            << vm->get_remote_system_dir() << "/disk." << i << endl;
    }

    //MV tm_mad prev_host:remote_system_dir host:remote_system_dir
    xfr << "MV "
        << system_tm_mad << " "
        << vm->get_previous_hostname() << ":" 
        << vm->get_remote_system_dir() << " "
        << vm->get_hostname() << ":" 
        << vm->get_remote_system_dir() << endl;

    xfr.close();

    tm_md->transfer(vid,xfr_name);

    vm->unlock();

    return;

error_history:
    os.str("");
    os << "prolog_migr, VM " << vid << " has no history";
    goto error_common;

error_file:
    os.str("");
    os << "prolog_migr, could not open file: " << xfr_name;
    goto error_common;

error_driver:
    os.str("");
    os << "prolog_migr, error getting Transfer Manager driver.";

error_common:
    (nd.get_lcm())->trigger(LifeCycleManager::PROLOG_FAILURE,vid);
    vm->log("TM", Log::ERROR, os);

    vm->unlock();
    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::prolog_resume_action(int vid)
{
    ofstream        xfr;
    ostringstream   os;
    string          xfr_name;

    const VectorAttribute * disk;
    string tm_mad;
    string system_tm_mad;

    vector<const Attribute *> attrs;
    int                       num;

    VirtualMachine *    vm;
    Nebula&             nd = Nebula::instance();

    const TransferManagerDriver * tm_md;

    // ------------------------------------------------------------------------
    // Setup & Transfer script
    // ------------------------------------------------------------------------

    system_tm_mad = nd.get_system_ds_tm_mad();

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    if (!vm->hasHistory())
    {
        goto error_history;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }

    xfr_name = vm->get_transfer_file() + ".resume";
    xfr.open(xfr_name.c_str(), ios::out | ios::trunc);

    if (xfr.fail() == true)
    {
        goto error_file;
    }

    // ------------------------------------------------------------------------
    // Move system directory and disks
    // ------------------------------------------------------------------------
    num = vm->get_template_attribute("DISK",attrs);

    for (int i=0 ; i < num ; i++, tm_mad="")
    {
        disk = dynamic_cast<const VectorAttribute *>(attrs[i]);

        if ( disk == 0 )
        {
            continue;
        }

        tm_mad = disk->vector_value("TM_MAD");

        if ( tm_mad.empty() )
        {
            continue;
        }

        //MV tm_mad fe:system_dir/disk.i host:remote_system_dir/disk.i
        xfr << "MV "
            << tm_mad << " "
            << nd.get_nebula_hostname() << ":" 
            << vm->get_system_dir() << "/disk." << i << " "
            << vm->get_hostname() << ":"
            << vm->get_remote_system_dir() << "/disk." << i << endl;
    }

    //MV tm_mad fe:system_dir host:remote_system_dir 
    xfr << "MV "
        << system_tm_mad << " "
        << nd.get_nebula_hostname() << ":"<< vm->get_system_dir() << " "
        << vm->get_hostname() << ":" << vm->get_remote_system_dir() << endl;
   
    xfr.close();

    tm_md->transfer(vid,xfr_name);

    vm->unlock();

    return;

error_history:
    os.str("");
    os << "prolog_resume, VM " << vid << " has no history";
    goto error_common;

error_file:
    os.str("");
    os << "prolog_resume, could not open file: " << xfr_name;
    goto error_common;

error_driver:
    os.str("");
    os << "prolog_resume, error getting Transfer Manager driver.";

error_common:
    (nd.get_lcm())->trigger(LifeCycleManager::PROLOG_FAILURE,vid);
    vm->log("TM", Log::ERROR, os);

    vm->unlock();
    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::epilog_action(int vid)
{
    ofstream        xfr;
    ostringstream   os;
    string          xfr_name;
    string          system_tm_mad;
    string          tm_mad;

    const VectorAttribute * disk;
    string          save;

    VirtualMachine *    vm;
    Nebula&             nd = Nebula::instance();

    const TransferManagerDriver * tm_md;

    vector<const Attribute *>   attrs;
    int                         num;

    // ------------------------------------------------------------------------
    // Setup & Transfer script
    // ------------------------------------------------------------------------

    system_tm_mad = nd.get_system_ds_tm_mad();

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    if (!vm->hasHistory())
    {
        goto error_history;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }

    xfr_name = vm->get_transfer_file() + ".epilog";
    xfr.open(xfr_name.c_str(), ios::out | ios::trunc);

    if (xfr.fail() == true)
    {
        goto error_file;
    }

    // ------------------------------------------------------------------------
    // copy back VM image (DISK with SAVE="yes")
    // ------------------------------------------------------------------------

    num = vm->get_template_attribute("DISK",attrs);

    for (int i=0; i < num ;i++,save="")
    {
        disk = dynamic_cast<const VectorAttribute *>(attrs[i]);

        if ( disk == 0 )
        {
            continue;
        }

        save = disk->vector_value("SAVE");

        if ( save.empty() == true)
        {
            continue;
        }

        transform(save.begin(),save.end(),save.begin(),(int(*)(int))toupper);

        tm_mad = disk->vector_value("TM_MAD");

        if ( save == "YES" ) //TODO SAVE_SOURCE
        {
            string        source;
            string        save_source;

            source      = disk->vector_value("SOURCE");
            save_source = disk->vector_value("SAVE_AS_SOURCE");

            if ( source.empty() && save_source.empty() )
            {
                vm->log("TM", Log::ERROR, "No SOURCE to save disk image");
                continue;
            }

            if (!save_source.empty()) //Use the save as source instead
            {
                source = save_source;
            }

            //MVDS tm_mad hostname:remote_system_dir/disk.0 <fe:SOURCE|SOURCE>
            xfr << "MVDS " 
                << tm_mad << " "
                << vm->get_hostname() << ":" 
                << vm->get_remote_system_dir() << "/disk." << i << " "
                << source << endl;
        }
        else if ( !tm_mad.empty() ) //No saving disk and no system_ds disk
        {
            //DELETE tm_mad hostname:remote_system_dir/disk.i
            xfr << "DELETE "
                << tm_mad << " "
                << vm->get_hostname() << ":"
                << vm->get_remote_system_dir() << "/disk." << i << endl;
        }
    }

    //DELETE system_tm_mad hostname:remote_system_dir
    xfr << "DELETE " 
        << system_tm_mad << " "
        << vm->get_hostname() << ":" << vm->get_remote_system_dir() << endl;
    
    xfr.close();

    tm_md->transfer(vid,xfr_name);

    vm->unlock();

    return;

error_history:
    os.str("");
    os << "epilog, VM " << vid << " has no history";
    goto error_common;

error_file:
    os.str("");
    os << "epilog, could not open file: " << xfr_name;
    goto error_common;

error_driver:
    os.str("");
    os << "epilog, error getting Transfer Manager driver.";

error_common:
    (nd.get_lcm())->trigger(LifeCycleManager::EPILOG_FAILURE,vid);
    vm->log("TM", Log::ERROR, os);

    vm->unlock();
    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::epilog_stop_action(int vid)
{
    ofstream      xfr;
    ostringstream os;
    string        xfr_name;
    string        tm_mad;
    string        system_tm_mad;

    VirtualMachine * vm;
    Nebula&          nd = Nebula::instance();

    const TransferManagerDriver * tm_md;

    vector<const Attribute *> attrs;
    const VectorAttribute *   disk;
    int                       num;

    // ------------------------------------------------------------------------
    // Setup & Transfer script
    // ------------------------------------------------------------------------
    system_tm_mad = nd.get_system_ds_tm_mad();

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    if (!vm->hasHistory())
    {
        goto error_history;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }

    xfr_name = vm->get_transfer_file() + ".stop";
    xfr.open(xfr_name.c_str(), ios::out | ios::trunc);

    if (xfr.fail() == true)
    {
        goto error_file;
    }

    // ------------------------------------------------------------------------
    // Move system directory and disks
    // ------------------------------------------------------------------------
    num = vm->get_template_attribute("DISK",attrs);

    for (int i=0 ; i < num ; i++, tm_mad="")
    {
        disk = dynamic_cast<const VectorAttribute *>(attrs[i]);

        if ( disk == 0 )
        {
            continue;
        }

        tm_mad = disk->vector_value("TM_MAD");

        if ( tm_mad.empty() )
        {
            continue;
        }

        //MV tm_mad host:remote_system_dir/disk.i fe:system_dir/disk.i
        xfr << "MV "
            << tm_mad << " "
            << vm->get_hostname() << ":"
            << vm->get_remote_system_dir() << "/disk." << i << " "
            << nd.get_nebula_hostname() << ":" 
            << vm->get_system_dir() << "/disk." << i  << endl;
    }

    //MV system_tm_mad hostname:remote_system_dir fe:system_dir
    xfr << "MV "
        << system_tm_mad << " "
        << vm->get_hostname() << ":" << vm->get_remote_system_dir() << " "
        << nd.get_nebula_hostname() << ":" << vm->get_system_dir() << endl;

    xfr.close();

    tm_md->transfer(vid,xfr_name);

    vm->unlock();

    return;

error_history:
    os.str("");
    os << "epilog_stop, VM " << vid << " has no history";
    goto error_common;

error_file:
    os.str("");
    os << "epilog_stop, could not open file: " << xfr_name;
    goto error_common;

error_driver:
    os.str("");
    os << "epilog_stop, error getting Transfer Manager driver.";

error_common:
    (nd.get_lcm())->trigger(LifeCycleManager::EPILOG_FAILURE,vid);
    vm->log("TM", Log::ERROR, os);

    vm->unlock();
    return;
}


/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::epilog_delete_action(int vid)
{
    ofstream      xfr;
    ostringstream os;
    string        xfr_name;
    string        system_tm_mad;
    string        tm_mad;

    VirtualMachine * vm;
    Nebula&          nd = Nebula::instance();

    const TransferManagerDriver * tm_md;

    const VectorAttribute *   disk;
    vector<const Attribute *> attrs;
    int                       num;

    // ------------------------------------------------------------------------
    // Setup & Transfer script
    // ------------------------------------------------------------------------
    system_tm_mad = nd.get_system_ds_tm_mad();

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    if (!vm->hasHistory())
    {
        goto error_history;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }
    
    xfr_name = vm->get_transfer_file() + ".delete";
    xfr.open(xfr_name.c_str(), ios::out | ios::trunc);

    if (xfr.fail() == true)
    {
        goto error_file;
    }

    // -------------------------------------------------------------------------
    // Delete disk images and the remote system Directory
    // -------------------------------------------------------------------------
    num = vm->get_template_attribute("DISK",attrs);

    for (int i=0 ; i < num ; i++, tm_mad="")
    {
        disk = dynamic_cast<const VectorAttribute *>(attrs[i]);

        if ( disk == 0 )
        {
            continue;
        }

        tm_mad = disk->vector_value("TM_MAD");

        if ( tm_mad.empty() )
        {
            continue;
        }

        //DELETE tm_mad host:remote_system_dir/disk.i
        xfr << "DELETE "
            << tm_mad << " "
            << vm->get_hostname() << ":"
            << vm->get_remote_system_dir() << "/disk." << i << endl;
    }

    //DELETE system_tm_mad hostname:remote_system_dir
    xfr << "DELETE " 
        << system_tm_mad << " "
        << vm->get_hostname() <<":"<< vm->get_remote_system_dir() << endl;

    xfr.close();

    tm_md->transfer(vid,xfr_name);

    vm->unlock();

    return;

error_history:
    os.str("");
    os << "epilog_delete, VM " << vid << " has no history";
    goto error_common;

error_file:
    os.str("");
    os << "epilog_delete, could not open file: " << xfr_name;
    os << ". You may need to manually clean " << vm->get_hostname() 
       << ":" << vm->get_remote_system_dir();
    goto error_common;

error_driver:
    os.str("");
    os << "epilog_delete, error getting driver Transfer Manager driver.";
    os << ". You may need to manually clean " << vm->get_hostname() 
       << ":" << vm->get_remote_system_dir();

error_common:
    vm->log("TM", Log::ERROR, os);
    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::epilog_delete_previous_action(int vid)
{
    ofstream      xfr;
    ostringstream os;
    string        xfr_name;
    string        system_tm_mad;
    string        tm_mad;

    VirtualMachine * vm;
    Nebula&          nd = Nebula::instance();

    const TransferManagerDriver * tm_md;

    const VectorAttribute *   disk;
    vector<const Attribute *> attrs;
    int                       num;

    // ------------------------------------------------------------------------
    // Setup & Transfer script
    // ------------------------------------------------------------------------

    system_tm_mad = nd.get_system_ds_tm_mad();

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    if (!vm->hasHistory() || !vm->hasPreviousHistory())
    {
        goto error_history;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }

    xfr_name = vm->get_transfer_file() + ".delete_prev";
    xfr.open(xfr_name.c_str(),ios::out | ios::trunc);

    if (xfr.fail() == true)
    {
        goto error_file;
    }

    // ------------------------------------------------------------------------
    // Delete the remote VM Directory
    // ------------------------------------------------------------------------
    num = vm->get_template_attribute("DISK",attrs);

    for (int i=0 ; i < num ; i++, tm_mad="")
    {
        disk = dynamic_cast<const VectorAttribute *>(attrs[i]);

        if ( disk == 0 )
        {
            continue;
        }

        tm_mad = disk->vector_value("TM_MAD");

        if ( tm_mad.empty() )
        {
            continue;
        }

        //DELETE tm_mad prev_host:remote_system_dir/disk.i
        xfr << "DELETE "
            << tm_mad << " "
            << vm->get_previous_hostname() << ":"
            << vm->get_remote_system_dir() << "/disk." << i << endl;
    }

    //DELTE system_tm_mad prev_host:remote_system_dir
    xfr << "DELETE " 
        << system_tm_mad << " "
        << vm->get_previous_hostname() <<":"<< vm->get_remote_system_dir()
        << endl;
    
    xfr.close();

    tm_md->transfer(vid,xfr_name);

    vm->unlock();

    return;

error_history:
    os.str("");
    os << "epilog_delete_previous, VM " << vid << " has no history";
    goto error_common;

error_file:
    os.str("");
    os << "epilog_delete, could not open file: " << xfr_name;
    os << ". You may need to manually clean " << vm->get_previous_hostname() 
       << ":" << vm->get_remote_system_dir();
    goto error_common;

error_driver:
    os.str("");
    os << "epilog_delete, error getting driver Transfer Manager driver.";
    os << ". You may need to manually clean " << vm->get_previous_hostname() 
       << ":" << vm->get_remote_system_dir();

error_common:
    vm->log("TM", Log::ERROR, os);
    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::driver_cancel_action(int vid)
{
    ofstream        xfr;
    ostringstream   os;
    string          xfr_name;

    VirtualMachine *    vm;

    const TransferManagerDriver * tm_md;

    // ------------------------------------------------------------------------
    // Get the Driver for this host
    // ------------------------------------------------------------------------

    vm = vmpool->get(vid,true);

    if (vm == 0)
    {
        return;
    }

    tm_md = get();

    if ( tm_md == 0 )
    {
        goto error_driver;
    }

    // ------------------------------------------------------------------------
    // Cancel the current operation
    // ------------------------------------------------------------------------
    
    tm_md->driver_cancel(vid);

    vm->unlock();

    return;

error_driver:
    os.str("");
    os << "driver_cancel, error getting driver Transfer Manager driver.";

    vm->log("TM", Log::ERROR, os);
    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void TransferManager::checkpoint_action(int vid)
{

}

/* ************************************************************************** */
/* MAD Loading                                                                */
/* ************************************************************************** */

void TransferManager::load_mads(int uid)
{
    ostringstream oss;

    int    rc;
    string name;

    const VectorAttribute * vattr = 0;
    TransferManagerDriver * tm_driver = 0;

    oss << "Loading Transfer Manager driver.";

    NebulaLog::log("TM",Log::INFO,oss);

    if ( mad_conf.size() > 0 )
    {
        vattr = static_cast<const VectorAttribute *>(mad_conf[0]);
    }

    if ( vattr == 0 )
    {
        NebulaLog::log("TM",Log::ERROR,"Failed to load Transfer Manager driver.");
        return;
    }

    VectorAttribute tm_conf("TM_MAD",vattr->value());

    tm_conf.replace("NAME",transfer_driver_name);

    tm_driver = new TransferManagerDriver(uid,
                                          tm_conf.value(),
                                          (uid != 0),
                                          vmpool);
    rc = add(tm_driver);

    if ( rc == 0 )
    {
        oss.str("");
        oss << "\tTransfer manager driver loaded";

        NebulaLog::log("TM",Log::INFO,oss);
    }
}
