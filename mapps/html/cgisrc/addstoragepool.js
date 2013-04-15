<!-- 

function IsNumeric(str){
	var re = /[\D]/

	if (re.test(str))
	{
		return false;
	}
	return true;
}

function ValidString(str){
	var re = /[^a-zA-Z0-9_\-]/

	if (re.test(str))
	{
		return false;
	}
	return true;
}

function checkform()
{
	var frm = document.getElementById('addstoragepool');

	if (!frm.groupname.value) {
		alert("Pool name cannot be empty");
		return false;
	}

	if (frm.groupname.value.length > 36) {
		alert("Pool name can be upto a maximum of 36 characters\n");
		return false;
	}

	if (!ValidString(frm.groupname.value))
	{
		alert("Pool name can only contains alphabets or numbers");
		return false;
	}

	return true;
}

// -->
