#include "dataXferImpl.h"
#include <string.h>

/** \file
 *  \brief Implementation of the \ref index "uC data transfer protocol".
 */

/// \name Command-finding state machine
//@{

/// The current state of the command-finding state machine.
static CMD_STATE cmdState;

/// The current output of the command-finding state machine
static CMD_OUTPUT cmdOutput;

void resetCommandFindMachine() {
  cmdState = STATE_CMD_START;
  cmdOutput = OUTPUT_CMD_NONE;
}

CMD_OUTPUT stepCommandFindMachine(char c_inChar, char* c_outChar) {
  switch (cmdState) {
    case STATE_CMD_START :
      if (c_inChar != CMD_TOKEN) {
        // This is just a character, not a command.
        *c_outChar = c_inChar;
        cmdOutput = OUTPUT_CMD_CHAR;
      } else {
        cmdState = STATE_CMD_WAIT1;
        cmdOutput = OUTPUT_CMD_NONE;
      }
      break;

      // We're waiting for a command or an escaped command.
    case STATE_CMD_WAIT1 :
      switch (c_inChar) {
        case CMD_TOKEN :
          // The sequence CMD_TOKEN CMD_TOKEN.
          cmdState = STATE_CMD_WAIT2;
          break;

        case ESCAPED_CMD :
          // This was an escaped CMD_TOKEN char.
          *c_outChar = CMD_TOKEN;
          cmdOutput = OUTPUT_CMD_CHAR;
          cmdState = STATE_CMD_START;
          break;

        default :
          // This is a command
          *c_outChar = c_inChar;
          cmdOutput = OUTPUT_CMD_CMD;
          cmdState = STATE_CMD_START;
          break;
      }
      break;

    case STATE_CMD_WAIT2 :
      switch (c_inChar) {
        case ESCAPED_CMD :
          // The sequence CMD_TOKEN CMD_TOKEN ESCAPED_CMD, which
          // is the command CMD_TOKEN
          cmdOutput = OUTPUT_CMD_CMD;
          cmdState = STATE_CMD_START;
          *c_outChar = CMD_TOKEN;
          break;

        case CMD_TOKEN :
          // The sequence CMD_TOKEN CMD_TOKEN CMD_TOKEN, which
          // is a repeated command (the first CMD_TOKEN) followed
          // by CMD_TOKEN CMD_TOKEN (so remain in this state to
          // decode it based on the next incoming char).
          cmdOutput = OUTPUT_CMD_REPEATED_WAIT;
          break;

        default :
          // The sequence CMD_TOKEN CMD_TOKEN c,
          // which is a repeated command c.
          *c_outChar = c_inChar;
          cmdOutput = OUTPUT_CMD_REPEATED_CMD;
          cmdState = STATE_CMD_START;
          break;

      }
      break;

    default:
      // Shouldn't happen
      ASSERT(FALSE);
      break;
  }

  return cmdOutput;
}
//@}



XFER_VAR xferVar[NUM_XFER_VARS];
uint8_t au8_xferVarWriteable[NUM_XFER_VARS/8 + ((NUM_XFER_VARS % 8) > 0)];


/// \name Receive state machine
//@{

/// Current state of the receive state machine.
static RECEIVE_STATE receiveState;

/// A character output by the receive state machine. See
/// \ref stepReceiveMachine for more information.
static char c_outChar;

/// The index of a data item; updated by the receive state
/// machine, or \ref CHAR_RECEIVED_INDEX if a character was received.
/// See \ref stepReceiveMachine for more information.
static uint u_index;

/// An error code produced by the receive
/// state machine. See \ref stepReceiveMachine for more information.
static RECEIVE_ERROR receiveError;

#ifndef PIC
/// True if the data just received by the receive state machine
/// was a specification. See \ref stepReceiveMachine for more information.
static BOOL b_isSpec;
#define B_IS_SPEC b_isSpec
#else
// For the PIC, we never have a spec.
#define B_IS_SPEC 0
#endif

RECEIVE_STATE getReceiveMachineState() {
  return receiveState;
}

char getReceiveMachineOutChar() {
  return c_outChar;
}

uint getReceiveMachineIndex() {
  return u_index;
}


RECEIVE_ERROR getReceiveMachineError() {
  RECEIVE_ERROR re = receiveError;
  receiveError = ERR_NONE;
  return re;
}


#if !defined(PIC) || defined(__DOXYGEN__)
BOOL getReceiveMachineIsSpec() {
  return b_isSpec;
}
#endif


void resetReceiveMachine() {
  receiveState = STATE_RECV_START;
  receiveError = ERR_NONE;
  // Since this state machine depends on the command-finding machine
  // reset it also.
  resetCommandFindMachine();
}


void clearReceiveMachineError() {
  receiveError = ERR_NONE;
}

#ifndef PIC
/** Free memory associted with a variable.
 *
 *  \param u_varIndex    A value from 0-\ref NUM_XFER_VARS, unique for each var.
 */
static void freeVariable(uint u_index) {
  if (xferVar[u_index].pu8_data != NULL) {
    free(xferVar[u_index].pu8_data);
    free(xferVar[u_index].psz_format);
    free(xferVar[u_index].psz_name);
    free(xferVar[u_index].psz_desc);
  }
}
#endif

void clearReceiveStruct() {
#ifndef PIC
  // Free all memory allocated for receive variables.
  uint u_index;
  for (u_index = 0; u_index < NUM_XFER_VARS; ++u_index) {
    freeVariable(u_index);
  }
#endif
  memset(xferVar, 0, sizeof(xferVar));
  memset(au8_xferVarWriteable, 0, sizeof(au8_xferVarWriteable));
}

BOOL isReceiveMachineChar() {
  return ( (receiveState == STATE_RECV_START) && (receiveError == ERR_NONE) &&
           (u_index == CHAR_RECEIVED_INDEX) );
}


BOOL isReceiveMachineData() {
  return ( (receiveState == STATE_RECV_START) && (receiveError == ERR_NONE) &&
           (u_index != CHAR_RECEIVED_INDEX) && !B_IS_SPEC);
}


#if !defined(PIC) || defined(__DOXYGEN__)
BOOL isReceiveMachineSpec() {
  return ( (receiveState == STATE_RECV_START) && (receiveError == ERR_NONE) &&
           (u_index != CHAR_RECEIVED_INDEX) && B_IS_SPEC);
}
#endif


uint getVarIndex(char c_cmd) {
  return ((unsigned char) c_cmd) >> VAR_SIZE_BITS;
}

uint getVarLength(char c_cmd) {
  return (c_cmd & VAR_SIZE_MASK) + 1;
}

/** Check that the given index is valid. The index is specified by
 *  \ref u_index. On error, the \ref receiveError and \ref receiveState
 *  variables are updated.
 *  \return A pointer to the data corresponding to the index if the index
 *          is valid, or NULL if an error occurred.
 */
static uint8_t* validateIndex() {
  uint8_t* pu8_data = NULL;

  // Check that the index exists
  if (u_index >= NUM_XFER_VARS) {
    receiveError = ERR_INDEX_TOO_HIGH;
    receiveState = STATE_RECV_START;
    return NULL;
  }

  // Set up to read length bytes in during
  // subsequent calls.
  pu8_data = xferVar[u_index].pu8_data;
  // If this index isn't configured, issue an error and abort
  if (pu8_data == NULL) {
    receiveError = ERR_UNSPECIFIED_INDEX;
    receiveState = STATE_RECV_START;
    return NULL;
  }

  // If this variable is read-only, issue an error and abort
#ifdef PIC
  if (!isVarWriteable(u_index)) {
    receiveError = ERR_READ_ONLY_VAR;
    receiveState = STATE_RECV_START;
    return NULL;
  }
#endif

  // Indicate success by returning a pointer to the data
  return pu8_data;
}

void assignBit(uint u_index, BOOL b_bitVal) {
  // Determine which byte this bit lives in
  uint u_byteIndex = u_index / 8;
  // Create a bit mask for this bit
  uint8_t u8_mask = 1 << (u_index % 8);
  // Set or clear it
  if (b_bitVal)
    au8_xferVarWriteable[u_byteIndex] |= u8_mask;
  else
    au8_xferVarWriteable[u_byteIndex] &= ~u8_mask;
}

BOOL isVarWriteable(uint u_index) {
  // Determine which byte this bit lives in
  uint u_byteIndex = u_index / 8;
  // Create a bit mask for this bit
  uint8_t u8_mask = 1 << (u_index % 8);
  // Read it
  return (au8_xferVarWriteable[u_byteIndex] & u8_mask) != 0;
}

/** Verify that the length of the variable matches the specified length.
 *  Otherwise, issue an error.
 *  On error, \ref receiveError and \ref receiveState are set.
 *  \param u_varLength The length of the variable, in bytes.
 *  \return TRUE on success, FALSE when the length does not match.
 */
static BOOL validateLength(uint u_varLength) {
  if (xferVar[u_index].u8_size != (u_varLength - 1)) {
    receiveError = ERR_VAR_SIZE_MISMATCH;
    receiveState = STATE_RECV_START;
    return FALSE;
  } else {
    return TRUE;
  }
}

#ifndef PIC
/// Temporary storage for a variable spec being received, with
/// enough additional space to terminate 3 unterminated strings for safety.
static uint8_t au8_varSpecData[(UINT8_MAX + 1) + 3];

/// The length of the var spec data
static uint u_specLength;

/** Parses a received variable spec, stored in \ref au8_varSpecData, assigns
 *  fields in \ref xferVar, and reports any errors.
 */
static void parseVarSpec() {
  uint u_size;
  size_t st_len;
  XFER_VAR* pXferVar = xferVar + u_index;
  char* psz_s;

  // Free old memory associated with this variable
  freeVariable(u_index);

  // Determine the size of this variable then allocate it. Note that
  // the size stored is the actual size minus one!
  u_size = pXferVar->u8_size = au8_varSpecData[0];
  u_size++;
  pXferVar->pu8_data = (uint8_t*) malloc(sizeof(uint8_t)*u_size);

  // Force all three strings to be null-terminated.
  ASSERT(u_specLength + 2 < sizeof(au8_varSpecData));
  au8_varSpecData[u_specLength + 0] = 0;
  au8_varSpecData[u_specLength + 1] = 0;
  au8_varSpecData[u_specLength + 2] = 0;

  // Copy the format string of the variable
  psz_s = (char*) au8_varSpecData + 1;
  st_len = strlen(psz_s) + 1;
  pXferVar->psz_format = (char*) malloc(sizeof(char)*st_len);
  strcpy(pXferVar->psz_format, psz_s);
  psz_s += st_len;
  ASSERT(((uint8_t*) psz_s) < au8_varSpecData + sizeof(au8_varSpecData));

  // Copy the name string of the variable
  st_len = strlen(psz_s) + 1;
  pXferVar->psz_name = (char*) malloc(sizeof(char)*st_len);
  strcpy(pXferVar->psz_name, psz_s);
  psz_s += st_len;
  ASSERT(((uint8_t*) psz_s) < au8_varSpecData + sizeof(au8_varSpecData));

  // Copy the description string of the variable
  st_len = strlen(psz_s) + 1;
  pXferVar->psz_desc = (char*) malloc(sizeof(char)*st_len);
  strcpy(pXferVar->psz_desc, psz_s);
  psz_s += st_len;
  ASSERT(((uint8_t*) psz_s) < au8_varSpecData + sizeof(au8_varSpecData));
}
#endif

RECEIVE_ERROR
notifyOfTimeout() {
  // Global state transition: a timeout.
  // Look for a timeout; they only occur during reception, not
  // in the start state.
  if (receiveState != STATE_RECV_START) {
    // Reset the machine, then signal the error (since errors
    // are cleared during reset).
    resetReceiveMachine();
    receiveError = ERR_TIMEOUT;
  }
  return receiveError;
}

RECEIVE_ERROR stepReceiveMachine(char c_inChar) {
  // Output of the command-finding state machine
  CMD_OUTPUT cmdOutput;
  // Bytes of a variable that still need to be read
  static uint u_varLength;
  // A pointer which variable data is read into
  static uint8_t* pu8_data = NULL;
#ifndef PIC
  // The last command received
  static char c_lastCommand;
#endif

  // Global state transition: waiting for a char or command
  // Run a char through the command state machine
  cmdOutput = stepCommandFindMachine(c_inChar, &c_outChar);
  // If we're waiting for another char, do that here instead of
  // adding a wait case to every receive state below.
  // If this is the start state (which signals data out is valid),
  // move out of that start state.
  switch (cmdOutput) {
    case OUTPUT_CMD_NONE :
      if (receiveState == STATE_RECV_START) {
        receiveState = STATE_RECV_CMD_WAIT;
      }
      return receiveError;

    case OUTPUT_CMD_REPEATED_CMD :
      // Report the repeated command, then begin processing
      // the second command as a (usual) command.
      receiveError = ERR_REPEATED_CMD;
      cmdOutput = OUTPUT_CMD_CMD;
      break;

    case OUTPUT_CMD_REPEATED_WAIT :
      // Report the repeated command, then wait
      // for another character.
      receiveError = ERR_REPEATED_CMD;
      return receiveError;

    case OUTPUT_CMD_CMD :
      // If we're not expecting a command, then signal an
      // interrupted command then move the state machine to
      // accept the command.
      if (receiveState != STATE_RECV_CMD_WAIT) {
        receiveError = ERR_INTERRUPTED_CMD;
        receiveState = STATE_RECV_CMD_WAIT;
      }
      break;

    case OUTPUT_CMD_CHAR :
      // These will be processed by the state machine below.
      break;

    default :
      ASSERT(FALSE);
      break;
  }

  // Process a command or a character using the remainer of the
  // receive state machine.
  switch (receiveState) {
    case STATE_RECV_START:
      switch (cmdOutput) {
        case OUTPUT_CMD_CHAR :
          // c_outChar is already assigned. Stay in this state.
          // Reset the error on a valid received character, unless this
          // this character caused a timeout.
          if (receiveError != ERR_TIMEOUT)
            receiveError = ERR_NONE;
          u_index = CHAR_RECEIVED_INDEX;
          break;

        default :
          // Commands and wait shouldn't be possible; any other
          // state is invalid.
          ASSERT(FALSE);
          break;
      }
      break;

    case STATE_RECV_CMD_WAIT :
      switch (cmdOutput) {
        case OUTPUT_CMD_CHAR :
          // c_outChar is already assigned.
          // Signal a character (the command token escaped) was received.
          ASSERT(c_outChar == CMD_TOKEN);
          receiveState = STATE_RECV_START;
          receiveError = ERR_NONE;
          u_index = CHAR_RECEIVED_INDEX;
          break;

        case OUTPUT_CMD_CMD :
          // Decode this command
          switch (c_outChar) {
            case CMD_LONG_VAR :
              receiveState = STATE_RECV_LONG_INDEX;
              break;

            case CMD_SEND_ONLY :
            case CMD_SEND_RECEIVE_VAR :
#ifdef PIC
              // The PIC should never receive a variable spec
              receiveState = STATE_RECV_START;
              receiveError = ERR_PIC_VAR_SPEC;
              break;
#else
              // Record this command, for use in the next state.
              c_lastCommand = c_outChar;
              receiveState = STATE_RECV_SPEC_INDEX;
              break;
#endif

            case ESCAPED_CMD :
              // Should never reach this state.
              ASSERT(FALSE);
              break;

            default :
              // It's not one of the special codes, so determine
              // the variable number and length.
              u_index = getVarIndex(c_outChar);
              u_varLength = getVarLength(c_outChar);
#ifndef PIC
              b_isSpec = FALSE;
#endif
              // Validate the index, getting a pointer to the data
              pu8_data = validateIndex();
              // If the index is invalid, go no further.
              if (pu8_data == NULL)
                break;
              // Check that the variable length matches
              if (!validateLength(u_varLength))
                break;
              // All checks passed; move to the receive state to
              // receive bytes.
              receiveState = STATE_RECV_READ_BYTES;
              break;
          }
          break;

        default :
          ASSERT(FALSE);
          break;
      }
      break;

    case STATE_RECV_READ_BYTES :
      // Save data to variable
      *pu8_data++ = c_outChar;
      // When all data is saved, indicate that
      // output is available
      if (--u_varLength == 0) {
        receiveError = ERR_NONE;
        receiveState = STATE_RECV_START;
#ifndef PIC
        if (b_isSpec)
          parseVarSpec();
#endif
      }
      break;

    case STATE_RECV_LONG_INDEX :
      // Save the index of this variable
      u_index = c_outChar;
#ifndef PIC
      b_isSpec = FALSE;
#endif
      // Validate the index, getting a pointer to the data
      pu8_data = validateIndex();
      // If the index is invalid, go no further.
      if (pu8_data == NULL)
        break;
      // Read the variable's length next
      receiveState = STATE_RECV_LONG_LENGTH;
      break;

#ifndef PIC
    case STATE_RECV_SPEC_INDEX :
      // Save the index of this variable
      u_index = c_outChar;
      b_isSpec = TRUE;
      // Point to the var spec temp buffer
      pu8_data = au8_varSpecData;
      // Record the type of this variable: send/receive or just send
      ASSERT( (c_lastCommand == CMD_SEND_ONLY) ||
              (c_lastCommand == CMD_SEND_RECEIVE_VAR) );
      assignBit(u_index, c_lastCommand == CMD_SEND_RECEIVE_VAR);
      // Read the variable's length next
      receiveState = STATE_RECV_LONG_LENGTH;
      break;
#endif

    case STATE_RECV_LONG_LENGTH :
      // Receive and record the length.
      // Subtle bug: the statement
      //  u_varLength = ((uint) c_outChar) + 1;
      // doesn't work:  C first converts c_outChar to a int, then from that to
      // a uint. Therefore, if c_outChar < 0, sign extension occurs, which is
      // incorrect. The syntax below first converts to a uint8_t then grows that
      // to a uint to guarantee zero extension rather than sign extension.
      u_varLength = ((uint) ((uint8_t) c_outChar)) + 1;
      // Validate it only if this isn't a variable spec
      if (!B_IS_SPEC && !validateLength(u_varLength))
        break;
#ifndef PIC
      else
        // Otherwise, record the number of bytes in the spec for later processing
        // in parseVarSpec.
        u_specLength = u_varLength;
#endif
      // Read the data
      receiveState = STATE_RECV_READ_BYTES;
      break;

    default:
      ASSERT(FALSE);
      break;
  }

  return receiveError;
}
//@}


/// Strings which provide a user-readable version of
/// the error codes in \ref RECEIVE_ERROR.
static const char* apsz_errorDesc[NUM_ERROR_CODES] = {
  "no error",
  "repeated command",
  "timeout",
  "interrupted command",
  "unspecified index",
  "index too high",
  "variable size mismatch",
  "read only variable",
  "illegal variable specification",
};

const char* getReceiveErrorString() {
  ASSERT( (receiveError > 0) && (receiveError < NUM_ERROR_CODES) );
  return apsz_errorDesc[receiveError];
}
