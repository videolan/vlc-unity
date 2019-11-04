using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CharacterControl : MonoBehaviour
{

    private Vector3 moveDirection = Vector3.zero;
    private Vector3 rotationDirection = Vector3.zero;
    private float moveSpeed = 10f;
    private float rotationSpeed = 30f;

    public VJHandler jsMovement;
    public VJHandler jsLook;
    private Vector3 direction;
    private Vector3 look;

    private void Update ()
    { 
        CharacterController controller = GetComponent<CharacterController> ();
        /*
        moveDirection = transform.TransformDirection (Input.GetAxis ("Horizontal"), 0, Input.GetAxis ("Vertical"));
        moveDirection *= moveSpeed;
        controller.Move (moveDirection * Time.deltaTime);
        rotationDirection = new Vector3 (Input.GetAxisRaw ("Vertical2"), Input.GetAxisRaw ("Horizontal2"), 0);
        rotationDirection *= rotationSpeed * Time.deltaTime;
        transform.Rotate (rotationDirection);
        */
        look = jsLook.InputDirection;
        if (look.magnitude != 0) {
            rotationDirection = new Vector3 (-look.y, look.x, 0) * rotationSpeed * Time.deltaTime;
            transform.eulerAngles = transform.eulerAngles + rotationDirection;
        }

        direction = jsMovement.InputDirection;
        if (direction.magnitude != 0) 
        {
            moveDirection = new Vector3 (direction.x, 0, direction.y) * moveSpeed;
            moveDirection = Camera.main.transform.TransformDirection(moveDirection);
            moveDirection.y = 0;
            controller.Move (moveDirection * Time.deltaTime);

            //transform.position += new Vector3 (direction.x, 0, direction.y) * moveSpeed * Time.deltaTime;
        }
    }
}