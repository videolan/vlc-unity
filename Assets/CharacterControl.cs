using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CharacterControl : MonoBehaviour {

     private Vector3 moveDirection = Vector3.zero;
     private Vector3 rotationDirection = Vector3.zero;
     private float moveSpeed = 5;
     private float rotationSpeed = 15;


     private void Update()
	  {
	       CharacterController controller = GetComponent<CharacterController>();

	       moveDirection = transform.TransformDirection(Input.GetAxis("Horizontal"), 0, Input.GetAxis("Vertical"));
	       moveDirection *= moveSpeed;
	       controller.Move(moveDirection * Time.deltaTime);

	       rotationDirection = new Vector3(Input.GetAxisRaw("Vertical2"), Input.GetAxisRaw("Horizontal2"), 0);
	       rotationDirection *= rotationSpeed * Time.deltaTime;
	       transform.Rotate(rotationDirection);
	  }
}
