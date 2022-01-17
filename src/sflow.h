/**
 * @file sflow.h
 * @author G.Linaris (lgilfanov@akber-soft.ru)
 * @brief Header for sflow.c
 * @date 2021-07-14
 *
 * @copyright (C) 2021, AkBer-Soft LLC
 *
 */

#include <debug.h>
#include <control-proto.h>

/**
 * @brief Global Enable/Disable Sampling To CPU (STC).
 *
 * @param[in] type Sampling To CPU (STC) type: ingress, egress or both
 * @param[in] enable true = enable Sampling To CPU (STC),
 *                   false = disable Sampling To CPU (STC)
 * @return execution status
 */
enum status sflow_set_enable (sflow_type_t type,
                              int enable);

/**
 * @brief Set the type of packets subject to Ingress Sampling to CPU.
 *
 * @param[in] mode ALL_PACKETS - All packets without any MAC-level errors,
 *                 NON_DROPPED_PACKETS - only non-dropped packets
 * @return execution status
 */
enum status sflow_set_ingress_count_mode (sflow_count_mode_t mode);

/**
 * @brief Set the type of Sampling To CPU (STC) count reload mode.
 *
 * @param[in] type Sampling To CPU (STC) type: ingress, egress or both
 * @param[in] mode Sampling to CPU (STC) Reload mode,
 * *               RELOAD_CONTINUOUS - contiuous mode,
*                  RELOAD_TRIGGERED - triggered mode
 * @return execution status
 */
enum status sflow_set_reload_mode (sflow_type_t type,
                                   sflow_count_reload_mode_t mode);

/**
 * @brief Set Sampling to CPU (STC) limit per port.
 *        The limit to be loaded into the Count Down Counter.
 *        This counter is decremented for each packet received on this port and
 *        is subject to sampling according to the setting of STC Count mode.
 *        When this counter is decremented from 1 to 0, the packet causing this
 *        decrement is sampled to the CPU.
 *
 * @param[in] pid port ID
 * @param[in] type Sampling To CPU (STC) type: ingress, egress or both
 * @param[in] limit limit - Count Down Limit (0 - 0x3FFFFFFF),
 *                  when limit value is 1 - every packet is sampled to CPU,
 *                  when limit value is 0 - there is no sampling
 * @param[in] set_def_settings used when device is stacked,
 *                             true - default settings,
 *                             false - not default settings
 * @return execution status
 */
enum status sflow_set_port_limit (port_id_t pid,
                                  sflow_type_t type,
                                  uint32_t limit,
                                  bool_t set_def_settings);

/**
 * @brief Convert GT_STATUS to enum status
 *
 * @param[in] st status with type GT_STATUS
 * @return status with type enum status
 */
enum status convert_status (GT_STATUS st);
