/**
 * @file ARUPDATER_Downloader.c
 * @brief libARUpdater Downloader c file.
 * @date 23/05/2014
 * @author djavan.bertrand@parrot.com
 **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Error.h>
#include <libARUtils/ARUTILS_Http.h>
#include <libARDiscovery/ARDISCOVERY_Discovery.h>
#include "ARUPDATER_Manager.h"
#include "ARUPDATER_Downloader.h"
#include "ARUPDATER_Utils.h"
/* ***************************************
 *
 *             define :
 *
 *****************************************/
#define ARUPDATER_DOWNLOADER_TAG   "ARUPDATER_Downloader"

#define ARUPDATER_DOWNLOADER_SERVER_URL                    "download.parrot.com"
#define ARUPDATER_DOWNLOADER_BEGIN_URL                     "Drones/"
#define ARUPDATER_DOWNLOADER_PHP_URL                       "/update.php"
#define ARUPDATER_DOWNLOADER_PARAM_MAX_LENGTH              255
#define ARUPDATER_DOWNLOADER_VERSION_BUFFER_MAX_LENGHT     10
#define ARUPDATER_DOWNLOADER_PRODUCT_PARAM                 "?product="
#define ARUPDATER_DOWNLOADER_SERIAL_PARAM                  "&serialNo="
#define ARUPDATER_DOWNLOADER_VERSION_PARAM                 "&version="
#define ARUPDATER_DOWNLOADER_VERSION_SEPARATOR             "."
#define ARUPDATER_DOWNLOADER_DOWNLOADED_FILE_PREFIX        "tmp_"
#define ARUPDATER_DOWNLOADER_SERIAL_DEFAULT_VALUE          "0000"

#define ARUPDATER_DOWNLOADER_PHP_ERROR_OK                  "0"
#define ARUPDATER_DOWNLOADER_PHP_ERROR_UNKNOWN             "1"
#define ARUPDATER_DOWNLOADER_PHP_ERROR_FILE                "2"
#define ARUPDATER_DOWNLOADER_PHP_ERROR_PLF                 "3"
#define ARUPDATER_DOWNLOADER_PHP_ERROR_MAGIC_PLF           "4"
#define ARUPDATER_DOWNLOADER_PHP_ERROR_UPDATE              "5"

#define ARUPDATER_DOWNLOADER_CHUNK_SIZE                    255
#define ARUPDATER_DOWNLOADER_MD5_TXT_SIZE                  32
#define ARUPDATER_DOWNLOADER_MD5_HEX_SIZE                  16

#define ARUPDATER_DOWNLOADER_HTTP_HEADER                   "http://"


/* ***************************************
 *
 *             function implementation :
 *
 *****************************************/

eARUPDATER_ERROR ARUPDATER_Downloader_New(ARUPDATER_Manager_t* manager, const char *const rootFolder, ARSAL_MD5_Manager_t *md5Manager, ARUPDATER_Downloader_ShouldDownloadPlfCallback_t shouldDownloadCallback, void *downloadArg, ARUPDATER_Downloader_PlfDownloadProgressCallback_t progressCallback, void *progressArg, ARUPDATER_Downloader_PlfDownloadCompletionCallback_t completionCallback, void *completionArg)
{
    ARUPDATER_Downloader_t *downloader = NULL;
    eARUPDATER_ERROR err = ARUPDATER_OK;

    // Check parameters
    if ((manager == NULL) || (rootFolder == NULL) || (md5Manager == NULL))
    {
        err = ARUPDATER_ERROR_BAD_PARAMETER;
    }

    if(err == ARUPDATER_OK)
    {
        /* Create the downloader */
        downloader = malloc (sizeof (ARUPDATER_Downloader_t));
        if (downloader == NULL)
        {
            err = ARUPDATER_ERROR_ALLOC;
        }
    }

    if (err == ARUPDATER_OK)
    {
        if (manager->downloader != NULL)
        {
            err = ARUPDATER_ERROR_ALREADY_INITIALIZED;
        }
        else
        {
            manager->downloader = downloader;
        }
    }

    /* Initialize to default values */
    if(err == ARUPDATER_OK)
    {
        int rootFolderLength = strlen(rootFolder) + 1;
        char *slash = strrchr(rootFolder, ARUPDATER_MANAGER_FOLDER_SEPARATOR[0]);
        if ((slash != NULL) && (strcmp(slash, ARUPDATER_MANAGER_FOLDER_SEPARATOR) != 0))
        {
            rootFolderLength += 1;
        }
        downloader->rootFolder = (char*) malloc(rootFolderLength);
        strcpy(downloader->rootFolder, rootFolder);

        if ((slash != NULL) && (strcmp(slash, ARUPDATER_MANAGER_FOLDER_SEPARATOR) != 0))
        {
            strcat(downloader->rootFolder, ARUPDATER_MANAGER_FOLDER_SEPARATOR);
        }
        
        downloader->md5Manager = md5Manager;

        downloader->downloadArg = downloadArg;
        downloader->progressArg = progressArg;
        downloader->completionArg = completionArg;

        downloader->shouldDownloadCallback = shouldDownloadCallback;
        downloader->plfDownloadProgressCallback = progressCallback;
        downloader->plfDownloadCompletionCallback = completionCallback;

        downloader->isRunning = 0;
        downloader->isCanceled = 0;

        downloader->requestConnection = NULL;
        downloader->downloadConnection = NULL;
    }

    if (err == ARUPDATER_OK)
    {
        int resultSys = ARSAL_Mutex_Init(&manager->downloader->requestLock);

        if (resultSys != 0)
        {
            err = ARUPDATER_ERROR_SYSTEM;
        }
    }

    if (err == ARUPDATER_OK)
    {
        int resultSys = ARSAL_Mutex_Init(&manager->downloader->downloadLock);

        if (resultSys != 0)
        {
            err = ARUPDATER_ERROR_SYSTEM;
        }
    }

    /* delete the downloader if an error occurred */
    if (err != ARUPDATER_OK)
    {
        ARSAL_PRINT (ARSAL_PRINT_ERROR, ARUPDATER_DOWNLOADER_TAG, "error: %s", ARUPDATER_Error_ToString (err));
        ARUPDATER_Downloader_Delete (manager);
    }

    return err;
}


eARUPDATER_ERROR ARUPDATER_Downloader_Delete(ARUPDATER_Manager_t *manager)
{
    eARUPDATER_ERROR error = ARUPDATER_OK;
    if (manager == NULL)
    {
        error = ARUPDATER_ERROR_BAD_PARAMETER;
    }
    else
    {
        if (manager->downloader == NULL)
        {
            error = ARUPDATER_ERROR_NOT_INITIALIZED;
        }
        else
        {
            if (manager->downloader->isRunning != 0)
            {
                error = ARUPDATER_ERROR_THREAD_PROCESSING;
            }
            else
            {
                ARSAL_Mutex_Destroy(&manager->downloader->requestLock);
                ARSAL_Mutex_Destroy(&manager->downloader->downloadLock);

                free(manager->downloader->rootFolder);

                free(manager->downloader);
                manager->downloader = NULL;
            }
        }
    }

    return error;
}

void* ARUPDATER_Downloader_ThreadRun(void *managerArg)
{
    eARUPDATER_ERROR error = ARUPDATER_OK;

    ARUPDATER_Manager_t *manager = NULL;
    if (managerArg != NULL)
    {
        manager = (ARUPDATER_Manager_t*)managerArg;
    }
    else
    {
        error = ARUPDATER_ERROR_BAD_PARAMETER;
    }

    if (manager != NULL)
    {
        manager->downloader->isRunning = 1;
    }

    int version;
    int edit;
    int ext;
    eARUTILS_ERROR utilsError = ARUTILS_OK;
    char *device = NULL;
    char *deviceFolder = NULL;
    char *filePath = NULL;
    uint32_t dataSize;
    char **dataPtr = NULL;
    ARSAL_Sem_t requestSem;
    ARSAL_Sem_t dlSem;
    
    char *plfFolder = malloc(strlen(manager->downloader->rootFolder) + strlen(ARUPDATER_MANAGER_PLF_FOLDER) + 1);
    strcpy(plfFolder, manager->downloader->rootFolder);
    strcat(plfFolder, ARUPDATER_MANAGER_PLF_FOLDER);

    int product = 0;
    while ((error == ARUPDATER_OK) && (product < ARDISCOVERY_PRODUCT_MAX) && (manager->downloader->isCanceled == 0))
    {
        // for each product, check if update is needed
        uint16_t productId = ARDISCOVERY_getProductID(product);

        device = malloc(ARUPDATER_MANAGER_DEVICE_STRING_MAX_SIZE);
        snprintf(device, ARUPDATER_MANAGER_DEVICE_STRING_MAX_SIZE, "%04x", productId);

        dataPtr = malloc(sizeof(char*));


        // read the header of the plf file
        deviceFolder = malloc(strlen(plfFolder) + strlen(device) + strlen(ARUPDATER_MANAGER_FOLDER_SEPARATOR) + 1);
        strcpy(deviceFolder, plfFolder);
        strcat(deviceFolder, device);
        strcat(deviceFolder, ARUPDATER_MANAGER_FOLDER_SEPARATOR);

        char *fileName = malloc(1);
        error = ARUPDATER_Utils_GetPlfInFolder(deviceFolder, &fileName);
        if (error == ARUPDATER_OK)
        {
            // file path = deviceFolder + plfFilename + \0
            filePath = malloc(strlen(deviceFolder) + strlen(fileName) + 1);
            strcpy(filePath, deviceFolder);
            strcat(filePath, fileName);
            
            error = ARUPDATER_Utils_GetPlfVersion(filePath, &version, &edit, &ext);
        }
        // else if the file does not exist, force to download
        else if (error == ARUPDATER_ERROR_PLF_FILE_NOT_FOUND)
        {
            version = 0;
            edit = 0;
            ext = 0;
            error = ARUPDATER_OK;

            // also check that the directory exists
            FILE *dir = fopen(plfFolder, "r");
            if (dir == NULL)
            {
                mkdir(plfFolder, S_IRWXU);
            }
            else
            {
                fclose(dir);
            }

            dir = fopen(deviceFolder, "r");
            if (dir == NULL)
            {
                mkdir(deviceFolder, S_IRWXU);
            }
            else
            {
                fclose(dir);
            }
        }
        
        free(fileName);

        // init the request semaphore
        ARSAL_Mutex_Lock(&manager->downloader->requestLock);
        if (error == ARUPDATER_OK)
        {
            int resultSys = ARSAL_Sem_Init(&requestSem, 0, 0);
            if (resultSys != 0)
            {
                error = ARUPDATER_ERROR_SYSTEM;
            }
        }

        // init the connection
        if (error == ARUPDATER_OK)
        {
            manager->downloader->requestConnection = ARUTILS_Http_Connection_New(&requestSem, ARUPDATER_DOWNLOADER_SERVER_URL, 80, HTTPS_PROTOCOL_FALSE, NULL, NULL, &utilsError);
            if (utilsError != ARUTILS_OK)
            {
                ARUTILS_Http_Connection_Delete(&manager->downloader->requestConnection);
                manager->downloader->requestConnection = NULL;
                ARSAL_Sem_Destroy(&requestSem);
                error = ARUPDATER_ERROR_DOWNLOADER_ARUTILS_ERROR;
            }
        }
        ARSAL_Mutex_Unlock(&manager->downloader->requestLock);

        // request the php
        if (error == ARUPDATER_OK)
        {
            char buffer[ARUPDATER_DOWNLOADER_VERSION_BUFFER_MAX_LENGHT];
            // create the url params
            char *params = malloc(ARUPDATER_DOWNLOADER_PARAM_MAX_LENGTH);
            strncpy(params, ARUPDATER_DOWNLOADER_PRODUCT_PARAM, strlen(ARUPDATER_DOWNLOADER_PRODUCT_PARAM));
            strncat(params, device, strlen(device));

            strncat(params, ARUPDATER_DOWNLOADER_SERIAL_PARAM, strlen(ARUPDATER_DOWNLOADER_SERIAL_PARAM));
            strncat(params, ARUPDATER_DOWNLOADER_SERIAL_DEFAULT_VALUE, strlen(ARUPDATER_DOWNLOADER_SERIAL_DEFAULT_VALUE));

            strncat(params, ARUPDATER_DOWNLOADER_VERSION_PARAM, strlen(ARUPDATER_DOWNLOADER_VERSION_PARAM));
            sprintf(buffer,"%i",version);
            strncat(params, buffer, strlen(buffer));
            strncat(params, ARUPDATER_DOWNLOADER_VERSION_SEPARATOR, strlen(ARUPDATER_DOWNLOADER_VERSION_SEPARATOR));
            sprintf(buffer,"%i",edit);
            strncat(params, buffer, strlen(buffer));
            strncat(params, ARUPDATER_DOWNLOADER_VERSION_SEPARATOR, strlen(ARUPDATER_DOWNLOADER_VERSION_SEPARATOR));
            sprintf(buffer,"%i",ext);
            strncat(params, buffer, strlen(buffer));

            char *endUrl = malloc(strlen(ARUPDATER_DOWNLOADER_BEGIN_URL) + strlen(device) + strlen(ARUPDATER_DOWNLOADER_PHP_URL) + strlen(params) + 1);
            strcpy(endUrl, ARUPDATER_DOWNLOADER_BEGIN_URL);
            strcat(endUrl, device);
            strcat(endUrl, ARUPDATER_DOWNLOADER_PHP_URL);
            strcat(endUrl, params);

            utilsError = ARUTILS_Http_Get_WithBuffer(manager->downloader->requestConnection, endUrl, (uint8_t**)dataPtr, &dataSize, NULL, NULL);
            if (utilsError != ARUTILS_OK)
            {
                error = ARUPDATER_ERROR_DOWNLOADER_ARUTILS_ERROR;
            }

            ARSAL_Mutex_Lock(&manager->downloader->requestLock);
            if (error == ARUPDATER_OK)
            {
                utilsError = ARUTILS_Http_Connection_Cancel(manager->downloader->requestConnection);
                if (utilsError != ARUTILS_OK)
                {
                    error = ARUPDATER_ERROR_DOWNLOADER_ARUTILS_ERROR;
                }
            }

            if (error == ARUPDATER_OK)
            {
                ARUTILS_Http_Connection_Delete(&manager->downloader->requestConnection);
                manager->downloader->requestConnection = NULL;
                ARSAL_Sem_Destroy(&requestSem);
            }
            ARSAL_Mutex_Unlock(&manager->downloader->requestLock);

            free(endUrl);
            endUrl = NULL;
            free(params);
            params = NULL;
        }

        // check if data fetch from request is valid
        if (error == ARUPDATER_OK)
        {
            if (*dataPtr != NULL)
            {
                (*dataPtr)[dataSize] = '\0';
                if (strlen(*dataPtr) != dataSize)
                {
                    error = ARUPDATER_ERROR_DOWNLOADER_DOWNLOAD;
                }
            }
        }

        // check if plf file need to be updated
        if (error == ARUPDATER_OK)
        {
            char *data = *dataPtr;
            char *result;
            result = strtok(data, "|");

            if(strcmp(result, ARUPDATER_DOWNLOADER_PHP_ERROR_OK) == 0)
            {
                // no need to update the plf file
                if (manager->downloader->shouldDownloadCallback != NULL)
                {
                    manager->downloader->shouldDownloadCallback(manager->downloader->downloadArg, 0);
                }
            }
            else if (strcmp(result, ARUPDATER_DOWNLOADER_PHP_ERROR_UPDATE) == 0)
            {
                // need to update the plf file
                if (manager->downloader->shouldDownloadCallback != NULL)
                {
                    manager->downloader->shouldDownloadCallback(manager->downloader->downloadArg, 1);
                }
                char *downloadUrl = strtok(NULL, "|");
                char *remoteMD5 = strtok(NULL, "|");
                char *downloadEndUrl;
                char *downloadServer;
                char *downloadedFileName = strrchr(downloadUrl, ARUPDATER_MANAGER_FOLDER_SEPARATOR[0]);
                if(downloadedFileName != NULL && strlen(downloadedFileName) > 0)
                {
                    downloadedFileName = &downloadedFileName[1];
                }
                
                char *downloadedFilePath = malloc(strlen(deviceFolder) + strlen(ARUPDATER_DOWNLOADER_DOWNLOADED_FILE_PREFIX) + strlen(downloadedFileName) + 1);
                strcpy(downloadedFilePath, deviceFolder);
                strcat(downloadedFilePath, ARUPDATER_DOWNLOADER_DOWNLOADED_FILE_PREFIX);
                strcat(downloadedFilePath, downloadedFileName);

                // explode the download url into server and endUrl
                if (strncmp(downloadUrl, ARUPDATER_DOWNLOADER_HTTP_HEADER, strlen(ARUPDATER_DOWNLOADER_HTTP_HEADER)) != 0)
                {
                    error = ARUPDATER_ERROR_DOWNLOADER_PHP_ERROR;
                }

                // construct the url
                if (error == ARUPDATER_OK)
                {
                    char *urlWithoutHttpHeader = downloadUrl + strlen(ARUPDATER_DOWNLOADER_HTTP_HEADER);
                    const char delimiter = '/';

                    downloadEndUrl = strchr(urlWithoutHttpHeader, delimiter);
                    int serverLength = strlen(urlWithoutHttpHeader) - strlen(downloadEndUrl);
                    downloadServer = malloc(serverLength + 1);
                    strncpy(downloadServer, urlWithoutHttpHeader, serverLength);
                    downloadServer[serverLength] = '\0';
                }

                ARSAL_Mutex_Lock(&manager->downloader->downloadLock);
                // init the request semaphore
                if (error == ARUPDATER_OK)
                {
                    int resultSys = ARSAL_Sem_Init(&dlSem, 0, 0);
                    if (resultSys != 0)
                    {
                        error = ARUPDATER_ERROR_SYSTEM;
                    }
                }

                if (error == ARUPDATER_OK)
                {
                    manager->downloader->downloadConnection = ARUTILS_Http_Connection_New(&dlSem, downloadServer, 80, HTTPS_PROTOCOL_FALSE, NULL, NULL, &utilsError);
                    if (utilsError != ARUTILS_OK)
                    {
                        ARUTILS_Http_Connection_Delete(&manager->downloader->downloadConnection);
                        manager->downloader->downloadConnection = NULL;
                        ARSAL_Sem_Destroy(&dlSem);
                        error = ARUPDATER_ERROR_DOWNLOADER_ARUTILS_ERROR;
                    }
                }
                ARSAL_Mutex_Unlock(&manager->downloader->downloadLock);

                // download the file
                if (error == ARUPDATER_OK)
                {
                    utilsError = ARUTILS_Http_Get(manager->downloader->downloadConnection, downloadEndUrl, downloadedFilePath, manager->downloader->plfDownloadProgressCallback, manager->downloader->progressArg);
                    if (utilsError != ARUTILS_OK)
                    {
                        error = ARUPDATER_ERROR_DOWNLOADER_ARUTILS_ERROR;
                    }
                }

                ARSAL_Mutex_Lock(&manager->downloader->downloadLock);
                if (error == ARUPDATER_OK)
                {
                    utilsError = ARUTILS_Http_Connection_Cancel(manager->downloader->downloadConnection);
                    if (utilsError != ARUTILS_OK)
                    {
                        error = ARUPDATER_ERROR_DOWNLOADER_ARUTILS_ERROR;
                    }
                }

                if (error == ARUPDATER_OK)
                {
                    ARUTILS_Http_Connection_Delete(&manager->downloader->downloadConnection);
                    manager->downloader->downloadConnection = NULL;
                    ARSAL_Sem_Destroy(&dlSem);
                }
                ARSAL_Mutex_Unlock(&manager->downloader->downloadLock);

                // check md5 match
                if (error == ARUPDATER_OK)
                {
                    eARSAL_ERROR arsalError = ARSAL_MD5_Manager_Check(manager->downloader->md5Manager, downloadedFilePath, remoteMD5);
                    if(ARSAL_OK != arsalError)
                    {
                        // delete the downloaded file if md5 don't match
                        unlink(downloadedFilePath);
                        error = ARUPDATER_ERROR_DOWNLOADER_MD5_DONT_MATCH;
                    }
                }

                if (error == ARUPDATER_OK)
                {
                    // if the file didn't exist before, file path is NULL
                    if (filePath == NULL)
                    {
                        filePath = malloc(strlen(deviceFolder) + strlen(downloadedFileName) + 1);
                        strcpy(filePath, deviceFolder);
                        strcat(filePath, downloadedFileName);
                    }
                    if (rename(downloadedFilePath, filePath) != 0)
                    {
                        error = ARUPDATER_ERROR_DOWNLOADER_RENAME_FILE;
                    }
                }

                if (downloadServer != NULL)
                {
                    free(downloadServer);
                    downloadServer = NULL;
                }
                if (downloadedFilePath != NULL)
                {
                    free(downloadedFilePath);
                    downloadedFilePath = NULL;
                }
            }
            else
            {
                error = ARUPDATER_ERROR_DOWNLOADER_PHP_ERROR;
            }
        }
        if (deviceFolder != NULL)
        {
            free(deviceFolder);
            deviceFolder = NULL;
        }
        if (filePath != NULL)
        {
            free(filePath);
            filePath = NULL;
        }
        if (device != NULL)
        {
            free(device);
            device = NULL;
        }
        if (dataPtr != NULL)
        {
            free(dataPtr);
            dataPtr = NULL;
        }
        
        product++;
    }

    if (plfFolder != NULL)
    {
        free(plfFolder);
        plfFolder = NULL;
    }

    if (error != ARUPDATER_OK)
    {
        ARSAL_PRINT (ARSAL_PRINT_ERROR, ARUPDATER_DOWNLOADER_TAG, "error: %s", ARUPDATER_Error_ToString (error));
    }

    if (manager != NULL)
    {
        manager->downloader->isRunning = 0;
    }
    
    if (manager->downloader->plfDownloadCompletionCallback != NULL)
    {
        manager->downloader->plfDownloadCompletionCallback(manager->downloader->completionArg, error);
    }

    return (void*)error;
}

eARUPDATER_ERROR ARUPDATER_Downloader_CancelThread(ARUPDATER_Manager_t *manager)
{
    eARUPDATER_ERROR error = ARUPDATER_OK;
    int resultSys = 0;

    if (manager == NULL)
    {
        error = ARUPDATER_ERROR_BAD_PARAMETER;
    }

    if ((error == ARUPDATER_OK) && (manager->downloader == NULL))
    {
        error = ARUPDATER_ERROR_NOT_INITIALIZED;
    }

    if (error == ARUPDATER_OK)
    {
        manager->downloader->isCanceled = 1;

        ARSAL_Mutex_Lock(&manager->downloader->requestLock);
        if (manager->downloader->requestConnection != NULL)
        {
            ARUTILS_Http_Connection_Cancel(manager->downloader->requestConnection);
            ARUTILS_Http_Connection_Delete(&manager->downloader->requestConnection);
            manager->downloader->requestConnection = NULL;
        }
        ARSAL_Mutex_Unlock(&manager->downloader->requestLock);

        ARSAL_Mutex_Lock(&manager->downloader->downloadLock);
        if (manager->downloader->downloadConnection != NULL)
        {
            ARUTILS_Http_Connection_Cancel(manager->downloader->downloadConnection);
            ARUTILS_Http_Connection_Delete(&manager->downloader->downloadConnection);
            manager->downloader->downloadConnection = NULL;
        }
        ARSAL_Mutex_Unlock(&manager->downloader->downloadLock);

        if (resultSys != 0)
        {
            error = ARUPDATER_ERROR_SYSTEM;
        }
    }

    if (error == ARUPDATER_OK)
    {
        manager->downloader->isRunning = 0;
    }

    return error;
}
