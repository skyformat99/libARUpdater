/**
 * @file ARUPDATER_Uploader.h
 * @brief libARUpdater Uploader header file.
 * @date 23/05/2014
 * @author djavan.bertrand@parrot.com
 **/

#ifndef _ARUPDATER_UPLOADER_H_
#define _ARUPDATER_UPLOADER_H_

#include <libARUpdater/ARUPDATER_Manager.h>
#include <libARDiscovery/ARDISCOVERY_Discovery.h>
#include <libARDataTransfer/ARDATATRANSFER_Uploader.h>
#include <libARSAL/ARSAL_MD5_Manager.h>

typedef struct ARUPDATER_Uploader_t ARUPDATER_Uploader_t;

/**
 * @brief Progress callback of the upload
 * @param arg The pointer of the user custom argument
 * @param percent The percent size of the plf file already uploaded
 * @see ARUPDATER_Manager_CheckLocaleVersionThreadRun ()
 */
typedef void (*ARUPDATER_Uploader_PlfUploadProgressCallback_t) (void* arg, uint8_t percent);

/**
 * @brief Completion callback of the Plf upload
 * @param arg The pointer of the user custom argument
 * @param error The error status to indicate the plf upload status
 * @see ARUPDATER_Manager_CheckLocaleVersionThreadRun ()
 */
typedef void (*ARUPDATER_Uploader_PlfUploadCompletionCallback_t) (void* arg, eARUPDATER_ERROR error);

/**
 * @brief Create an object to upload a plf file
 * @warning this function allocates memory
 * @post ARUPDATER_Uploader_Delete should be called
 * @param manager : pointer on the manager
 * @param[in] rootFolder : root folder
 * @param[in] ftpManager : ftp manager initialized with the correct network (wifi or ble)
 * @param[in] md5Manager : md5 manager
 * @param[in] product : enumerator on the enum
 * @param[in] progressCallback : callback which tells the progress of the download
 * @param[in|out] progressArg : arg given to the progressCallback
 * @param[in] completionCallback : callback which tells when the download is completed
 * @param[in|out] completionArg : arg given to the completionCallback
 * @return ARUPDATER_OK if operation went well, a description of the error otherwise
 * @see ARUPDATER_Uploader_Delete()
 */
eARUPDATER_ERROR ARUPDATER_Uploader_New(ARUPDATER_Manager_t* manager, const char *const rootFolder, ARUTILS_Manager_t *ftpManager, ARSAL_MD5_Manager_t *md5Manager, eARDISCOVERY_PRODUCT product, ARUPDATER_Uploader_PlfUploadProgressCallback_t progressCallback, void *progressArg, ARUPDATER_Uploader_PlfUploadCompletionCallback_t completionCallback, void *completionArg);


/**
 * @brief Delete the Uploader of the Manager
 * @warning This function frees memory
 * @param manager a pointer on the ARUpdater Manager
 * @see ARUPDATER_Uploader_New ()
 */
eARUPDATER_ERROR ARUPDATER_Uploader_Delete(ARUPDATER_Manager_t *manager);

/**
 * @brief Upload a plf
 * @warning This function must be called in its own thread.
 * @post ARUPDATER_Uploader_CancelThread() must be called after.
 * @param managerArg : thread data of type ARUPDATER_Manager_t*
 * @return ARUPDATER_OK if operation went well, a description of the error otherwise
 * @see ARUPDATER_Uploader_CancelThread()
 */
void* ARUPDATER_Uploader_ThreadRun(void *managerArg);

/**
 * @brief cancel the upload
 * @details Used to kill the thread calling ARUPDATER_Uploader_ThreadRun().
 * @param manager : pointer on the manager
 * @see ARUPDATER_Uploader_ThreadRun()
 */
eARUPDATER_ERROR ARUPDATER_Uploader_CancelThread(ARUPDATER_Manager_t *manager);

#endif
